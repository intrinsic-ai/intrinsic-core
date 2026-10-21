# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Useful constants and helper functions for working with the content-addressable storage (CAS) service."""

from collections.abc import Iterator
from collections.abc import Sequence
import io
from typing import Any
from typing import List
from typing import Optional
from typing import Tuple

from absl import logging
import grpc
import tenacity

from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2
from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc

# Recommended size of content to be included into one stream request.
#
# Why 1 MiB?
# https://cloud.google.com/blog/products/gcp/optimizing-your-cloud-storage-performance-google-cloud-performance-atlas
# suggests that 1 MB+ is a good chunk size.
DEFAULT_UPLOAD_CHUNK_SIZE = 1 * 1024 * 1024  # 1 MiB

# Log a warning if user attempts to download a large file using
# client_helpers.get. The function is memory intensive and can be slow due
# to repeated memory re-allocation.
FILE_SIZE_THRESHOLD = 500 * 1024 * 1024  # 500 MiB.


class IncompleteDownloadError(RuntimeError):
  """Raised when a download terminates prematurely before receiving all expected bytes."""


_RETRIABLE_STATUS_CODES = {
    grpc.StatusCode.UNAVAILABLE,
    grpc.StatusCode.RESOURCE_EXHAUSTED,
    grpc.StatusCode.DEADLINE_EXCEEDED,
    grpc.StatusCode.ABORTED,
}


def _grpc_status_code(exc: BaseException) -> Optional[grpc.StatusCode]:
  """Returns the gRPC StatusCode if the error is a gRPC call exception."""
  return exc.code() if isinstance(exc, grpc.Call) else None


def _is_retriable(exc: BaseException) -> bool:
  """Returns True if the error is a retriable gRPC error or truncated stream."""
  return (
      isinstance(exc, IncompleteDownloadError)
      or _grpc_status_code(exc) in _RETRIABLE_STATUS_CODES
  )


class ResumableReader(io.BufferedIOBase):
  """Read-only binary stream over a CAS object that resumes on transient errors.

  Inherits from io.BufferedIOBase so instances can be passed directly to
  standard library stream consumers (e.g. tarfile, shutil.copyfileobj).
  Implements read1() to yield uncopied server chunks and inherits readinto()
  for caller-allocated buffers.

  Chunks are pulled lazily from the CAS service via GetRange starting at the
  configured offset. Transient gRPC failures (and streams that end before
  reaching the object's total size) are retried with exponential backoff by
  re-opening GetRange at the current offset. The retry budget applies per
  chunk: it is reset whenever a chunk is received successfully.

  If the server does not implement GetRange and the reader is at offset 0, it
  falls back to Get. Since Get always streams from byte 0, transient errors on
  the Get stream are only retried while no bytes have been consumed yet, and
  the total received size is checked against Stat.

  Once a terminal error occurs, the reader closes itself so subsequent calls
  fail fast with ValueError without issuing new RPCs. Reads after the end of
  the object return b"" without issuing new RPCs.

  Instances are not thread-safe. Use as a context manager (or call close()) to
  cancel the in-flight gRPC call. Prefer constructing instances with
  get_resumable_reader().
  """

  def __init__(
      self,
      object_id: str,
      *,
      cas_stub: (
          cas_service_pb2_grpc.ContentAddressableStorageServiceStub | None
      ) = None,
      start_offset: int = 0,
      max_retries: int | None = 5,
      initial_backoff_sec: float = 0.1,
      max_backoff_sec: float = 2.0,
      backoff_multiplier: float = 1.5,
      grpc_metadata: Sequence[Tuple[str, str]] | None = None,
  ):
    """Initializes the reader. See get_resumable_reader() for arguments."""
    # Active GetRange/Get response iterator (usually also a grpc.Call). Set
    # first because IOBase.__del__ calls close() even if __init__ raises.
    self._call: Iterator[Any] | None = None
    self._closed = False
    if cas_stub is None:
      raise ValueError("cas_stub must not be None.")
    if start_offset < 0:
      raise ValueError(f"Invalid start offset {start_offset}; must be >= 0.")
    if max_retries is not None and max_retries < 0:
      raise ValueError(
          f"Invalid max_retries {max_retries}; must be >= 0 or None."
      )
    super().__init__()
    self._stub = cas_stub
    self._object_id = object_id
    self._offset = start_offset
    self._grpc_kwargs = {"metadata": grpc_metadata} if grpc_metadata else {}
    self._retrying = tenacity.Retrying(
        stop=(
            tenacity.stop_never
            if max_retries is None
            else tenacity.stop_after_attempt(max_retries + 1)
        ),
        wait=tenacity.wait_random_exponential(
            multiplier=initial_backoff_sec,
            max=max_backoff_sec,
            exp_base=backoff_multiplier,
        ),
        retry=tenacity.retry_if_exception(self._can_retry),
        before_sleep=self._log_retry,
        reraise=True,
    )

    # Unread data of the most recently received chunk is _chunk[_pos:].
    self._chunk = b""
    self._pos = 0
    # Not all servers implement GetRange. Get is only usable from offset 0.
    self._use_get = False
    # Total object size as reported by GetRange or Stat, if known.
    self._total_size: int | None = None
    self._eof = False

  def readable(self) -> bool:
    """Returns True so stream consumers (e.g. shutil) accept this reader.

    io.IOBase defaults readable() to False, which causes standard library
    consumers to raise io.UnsupportedOperation before attempting to read.
    """
    self._check_open()
    return True

  def tell(self) -> int:
    """Returns the offset in the CAS object of the next byte to be read."""
    self._check_open()
    return self._offset

  def read(self, size: int | None = -1) -> bytes:
    """Reads up to size bytes; reads until the end of the object if size < 0.

    Fewer than size bytes are returned only at the end of the object.

    Args:
      size: Maximum number of bytes to read. None or negative reads all
        remaining bytes.

    Returns:
      The bytes read; b"" at the end of the object.
    """
    self._check_open()
    if size is None or size < 0:
      return b"".join(iter(self.read1, b""))
    parts = []
    remaining = size
    while remaining > 0:
      chunk = self.read1(remaining)
      if not chunk:
        break
      parts.append(chunk)
      remaining -= len(chunk)
    return b"".join(parts)

  def read1(self, size: int = -1) -> bytes:
    """Reads up to size bytes, pulling at most one chunk from the server.

    With size < 0, returns the remainder of the current chunk (or the whole next
    chunk) without copying. This is the most efficient way to stream an object:

      while chunk := reader.read1():
        writer.write(chunk)

    Args:
      size: Maximum number of bytes to read. Negative means "one chunk".

    Returns:
      The bytes read; b"" at the end of the object.
    """
    self._check_open()
    if size == 0:
      return b""
    # Loop so that empty chunks are skipped rather than reported as EOF.
    while not self._eof and self._pos >= len(self._chunk):
      self._pull()
    if self._eof:
      return b""
    if self._pos == 0 and (size < 0 or size >= len(self._chunk)):
      data = self._chunk
    else:
      end = len(self._chunk) if size < 0 else self._pos + size
      data = self._chunk[self._pos : end]
    self._pos += len(data)
    self._offset += len(data)
    return data

  def close(self) -> None:
    """Cancels the in-flight gRPC call (if any) and closes the reader."""
    if not self._closed:
      self._closed = True
      self._discard_call()
    super().close()

  def _check_open(self) -> None:
    """Raises ValueError if the reader has been closed.

    Python's io.IOBase contract requires stream methods (read, tell, readable)
    to fail fast with ValueError rather than initiating I/O after close().
    """
    if self._closed:
      raise ValueError("I/O operation on closed file.")

  def _pull(self) -> None:
    """Receives the next chunk (or EOF), retrying transient errors."""
    try:
      self._retrying(self._receive_next)
    except (grpc.RpcError, RuntimeError):
      self.close()
      raise

  def _can_retry(self, exc: BaseException) -> bool:
    # Get always streams from byte 0, so it can only be retried before any
    # bytes have been handed out to the caller.
    return _is_retriable(exc) and (not self._use_get or self._offset == 0)

  def _log_retry(self, retry_state: tenacity.RetryCallState) -> None:
    logging.warning(
        "Transient error during CAS download of %r at offset %d: %s."
        " Retrying (%d) in %.2fs...",
        self._object_id,
        self._offset,
        retry_state.outcome.exception() if retry_state.outcome else None,
        retry_state.attempt_number,
        retry_state.upcoming_sleep,
    )

  def _receive_next(self) -> None:
    """Receives the next response from the active stream (opening it if needed).

    On success, either buffers the received chunk or marks the end of the
    object.

    Raises:
      grpc.RpcError: If the RPC fails.
      IncompleteDownloadError: If the stream ends before the object's end.
      RuntimeError: If the server returns a chunk at an unexpected offset.
    """
    while True:
      try:
        call = self._call
        if call is None:
          call = self._call = self._open()
        response = next(call)
        break
      except StopIteration:
        response = None
        break
      except grpc.RpcError as e:
        self._discard_call()
        # Server-streaming RPCs usually report UNIMPLEMENTED on the first
        # next() rather than when the call is created.
        if (
            self._use_get
            or self._offset != 0
            or _grpc_status_code(e) != grpc.StatusCode.UNIMPLEMENTED
        ):
          raise
        logging.info(
            "GetRange is not implemented by the CAS server; falling back to"
            " Get for %r.",
            self._object_id,
        )
        self._use_get = True

    if response is None:
      self._call = None
      if self._total_size is not None and self._offset != self._total_size:
        raise IncompleteDownloadError(
            f"The CAS stream for '{self._object_id}' ended at offset"
            f" {self._offset}, but the object size is {self._total_size} bytes."
        )
      self._eof = True
      return

    if not self._use_get:
      if response.chunk_offset != self._offset:
        raise RuntimeError(
            f"Unexpected chunk offset for '{self._object_id}': got"
            f" {response.chunk_offset}, want {self._offset}."
        )
      if response.total_object_size:
        self._total_size = response.total_object_size
    self._chunk = response.checksummed_data.content
    self._pos = 0

  def _open(self) -> Iterator[Any]:
    """Opens a GetRange stream at the current offset (or a Get stream)."""
    if not self._use_get:
      return self._stub.GetRange(
          cas_service_pb2.GetRangeRequest(
              object_id=self._object_id, read_offset=self._offset
          ),
          **self._grpc_kwargs,
      )

    # Get responses don't carry the object size, so fetch it via Stat to
    # detect streams that end early without an error.
    if self._total_size is None:
      stat_response = self._stub.Stat(
          cas_service_pb2.StatRequest(object_id=self._object_id),
          **self._grpc_kwargs,
      )
      self._total_size = stat_response.size
    return self._stub.Get(
        cas_service_pb2.GetRequest(object_id=self._object_id),
        **self._grpc_kwargs,
    )

  def _discard_call(self) -> None:
    """Cancels and forgets the active stream so the next pull re-opens it."""
    call, self._call = self._call, None
    if call is None:
      return
    # Real calls are grpc.Call objects; intercepted calls may be generators.
    cancel = getattr(call, "cancel", None) or getattr(call, "close", None)
    if callable(cancel):
      cancel()


def get_resumable_reader(
    object_id: str,
    *,
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
    start_offset: int = 0,
    max_retries: int | None = 5,
    initial_backoff_sec: float = 0.1,
    max_backoff_sec: float = 2.0,
    backoff_multiplier: float = 1.5,
    grpc_metadata: Sequence[Tuple[str, str]] | None = None,
) -> ResumableReader:
  """Returns a ResumableReader streaming object_id from start_offset.

  The underlying gRPC stream is opened lazily on the first read. Errors from the
  CAS service are raised as unwrapped grpc.RpcError so that callers can inspect
  their status code.

  Args:
    object_id: CAS object ID.
    cas_stub: Stub for the content-addressable storage service.
    start_offset: Zero-based byte offset to start reading from.
    max_retries: Maximum consecutive retries per chunk before failing. None
      retries indefinitely.
    initial_backoff_sec: Initial backoff duration in seconds.
    max_backoff_sec: Maximum backoff duration in seconds.
    backoff_multiplier: Multiplier for exponential backoff.
    grpc_metadata: Optional gRPC metadata to attach to each RPC. Note: any
      credentials passed here are static and will not be refreshed. If
      credentials need to be auto-updated (e.g. for long-running downloads that
      may exceed 1 hour), pass None here and configure credentials on the
      channel used to initialize cas_stub.

  Returns:
    A ResumableReader. Close it (or use it as a context manager) when done.

  Raises:
    ValueError: If cas_stub is None, start_offset < 0, or max_retries < 0.

  Example:
    To ensure credentials auto-refresh across long downloads (> 1 hour),
    initialize ``cas_stub`` using an authenticated channel (e.g. via
    ``dialerutil.create_channel_from_org``) rather than passing static
    ``grpc_metadata``:

      from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc
      from intrinsic.storage.content_addressable_storage.python import client_helpers
      from intrinsic.util.grpc import auth, dialerutil

      org_info = auth.parse_info_from_string("<org>@<project>")
      with dialerutil.create_channel_from_org(org_info) as channel:
        stub = cas_service_pb2_grpc.ContentAddressableStorageServiceStub(channel)
        with client_helpers.get_resumable_reader(
            object_id, cas_stub=stub
        ) as reader:
          with tarfile.open(fileobj=reader, mode="r|") as tar:
            tar.extractall(dest_dir)
  """
  return ResumableReader(
      object_id,
      cas_stub=cas_stub,
      start_offset=start_offset,
      max_retries=max_retries,
      initial_backoff_sec=initial_backoff_sec,
      max_backoff_sec=max_backoff_sec,
      backoff_multiplier=backoff_multiplier,
      grpc_metadata=grpc_metadata,
  )


def get_iter(
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
    object_id: str,
    grpc_metadata: Optional[Sequence[Tuple[str, str]]] = None,
) -> Iterator[bytes]:
  """Generator to retrieve an object from CAS.

  Prefer this function when the object is expected to be large or when written
  directly to a file.

  Args:
    cas_stub: Stub for the content-addressable storage service.
    object_id: CAS object ID.
    grpc_metadata: Optional gRPC metadata to be attached to the request.

  Yields:
    A chunk of the object. Its size is determined by the server.
  """
  stat_request = cas_service_pb2.StatRequest(object_id=object_id)
  stat_response = cas_stub.Stat(stat_request, metadata=grpc_metadata)
  expected_total_size = stat_response.size

  request = cas_service_pb2.GetRequest(object_id=object_id)
  total_received_size = 0
  if grpc_metadata:
    for response in cas_stub.Get(request, metadata=grpc_metadata):
      total_received_size += len(response.checksummed_data.content)
      yield response.checksummed_data.content
  else:
    for response in cas_stub.Get(request):
      # TODO: b/289500064 - Add checksum check.
      total_received_size += len(response.checksummed_data.content)
      yield response.checksummed_data.content

  # Check if all bytes have arrived. This is done because there is no checksum
  # check over the whole blob when streamed and we have observed incomplete
  # downloads without raised exceptions before. This led to hard to debug
  # downstream failures.
  if total_received_size != expected_total_size:
    error_message = (
        f"The total expected size of the downloaded CAS blob '{object_id}' is"
        " not equal to the sum of received bytes over all chunks!"
        f" (expected={expected_total_size} | received={total_received_size})!"
    )
    logging.error(error_message)
    raise IncompleteDownloadError(error_message)


def get(
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
    object_id: str,
    grpc_metadata: Optional[List[Tuple[str, str]]] = None,
) -> bytes:
  """Retrieve an object from CAS into a bytes-like array.

  Prefer this function when the object is expected to be small and it's ok to
  allocate memory for it.

  Args:
    cas_stub: Stub for the content-addressable storage service.
    object_id: CAS object ID.
    grpc_metadata: Optional gRPC metadata to be attached to the request.

  Returns:
    A bytes-like array containing the object retrieved from CAS.
  """
  data = bytearray()
  warning_issued = False
  for chunk in get_iter(cas_stub, object_id, grpc_metadata):
    data += chunk
    if (not warning_issued) and (len(data) > FILE_SIZE_THRESHOLD):
      logging.warning(
          "client_helpers.get is downloading a large object in memory"
          " (>%.2f MiB) which can be slow and memory intensive. Prefer using"
          " client_helpers.get_iter to iteratively retrieve chunks of large"
          " files and save them to disk.",
          FILE_SIZE_THRESHOLD / (1024.0 * 1024.0),
      )
      warning_issued = True

  return bytes(data)


def create_from_reader(
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
    source: io.IOBase,
    chunk_size: int = DEFAULT_UPLOAD_CHUNK_SIZE,
    grpc_metadata: Optional[List[Tuple[str, str]]] = None,
) -> str:
  """Write data to CAS from an object that implements read().

  Prefer this function when the object is large or when you're reading from a
  file.

  Args:
    cas_stub: Stub for the content-addressable storage service.
    source: Any object that implements read().
    chunk_size: Chunk size used for the upload, should be between 1 MB and 4 MB.
    grpc_metadata: Optional gRPC metadata to be attached to the request.

  Returns:
    CAS object ID of the uploaded data.
  """
  requests = (
      _make_create_request(chunk)
      for chunk in iter(lambda: source.read(chunk_size), b"")
  )
  if grpc_metadata:
    response = cas_stub.Create(requests, metadata=grpc_metadata)
  else:
    response = cas_stub.Create(requests)
  return response.object_id


def _make_create_request(chunk: bytes) -> cas_service_pb2.CreateRequest:
  return cas_service_pb2.CreateRequest(
      checksummed_data=cas_service_pb2.ChecksummedData(
          content=chunk
          # TODO: b/289500064 - Add checksum check.
      )
  )


def create(
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
    data: bytes,
    chunk_size: int = DEFAULT_UPLOAD_CHUNK_SIZE,
    grpc_metadata: Optional[List[Tuple[str, str]]] = None,
) -> str:
  """Write data to CAS from an in-memory bytes-like object.

  Prefer this function for small objects that are held in memory.

  Args:
    cas_stub: Stub for the content-addressable storage service.
    data: A bytes-like array.
    chunk_size: Chunk size used for the upload, should be between 1 MB and 4 MB.
    grpc_metadata: Optional gRPC metadata to be attached to the request.

  Returns:
    CAS object ID of the uploaded data.
  """
  return create_from_reader(
      cas_stub, io.BytesIO(data), chunk_size, grpc_metadata
  )
