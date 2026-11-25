// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package jupyter provides message types and constants for communicating with a
// Jupyter server via its REST and websocket APIs. The REST API can be used to
// create and manage Jupyter kernels and corresponding sessions. Once a session
// has been created, one can establish a websocket connection "to the session's
// kernel" over which, e.g., messages for code execution can be sent and status
// updates and execution results can be received.
//
// For the REST API, see:
//
//	https://jupyter-server.readthedocs.io/en/latest/developers/rest-api.html
//
// For the websocket API, see:
//
//	https://jupyter-client.readthedocs.io/en/stable/messaging.html
package jupyter

import (
	"encoding/json"
	"fmt"
)

const (
	// Python3KernelName is the name of the default Python 3 kernel in Jupyter
	// which gets installed with the ipykernel package.
	Python3KernelName = "python3"

	// ExecutionStateStarting is the "starting" execution state of a Jupyter
	// kernel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#kernel-status
	ExecutionStateStarting = "starting"

	// ExecutionStateBusy is the "busy" execution state of a Jupyter kernel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#kernel-status
	ExecutionStateBusy = "busy"

	// ExecutionStateIdle is the "idle" execution state of a Jupyter kernel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#kernel-status
	ExecutionStateIdle = "idle"

	// ChannelShell is the name of the "shell" websocket channel of a Jupyter
	// kernel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#messages-on-the-shell-router-dealer-channel
	ChannelShell = "shell"

	// ChannelIopub is the name of the "iopub" websocket channel of a Jupyter
	// kernel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#messages-on-the-iopub-pub-sub-channel
	ChannelIopub = "iopub"

	// ChannelStdin is the name of the "stdin" websocket channel of a Jupyter
	// kernel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#messages-on-the-stdin-router-dealer-channel
	ChannelStdin = "stdin"

	// ChannelControl is the name of the "control" websocket channel of a Jupyter
	// kernel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#messages-on-the-control-router-dealer-channel
	ChannelControl = "control"

	// ChannelHeartbeat is the name of the "hb" websocket channel of a Jupyter
	// kernel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#heartbeat-for-kernels
	ChannelHeartbeat = "hb"

	// MsgTypeStatus is the "msg_type" of a "status" websocket message on the
	// "iopub" channel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#status
	MsgTypeStatus = "status"

	// MsgTypeError is the "msg_type" of an "error" websocket message on the
	// "iopub" channel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-errors
	MsgTypeError = "error"

	// MsgTypeExecuteRequest is the "msg_type" of an "execute_request" websocket
	// message on the "shell" channel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execute
	MsgTypeExecuteRequest = "execute_request"

	// MsgTypeExecuteReply is the "msg_type" of an "execute_reply" websocket
	// message on the "shell" channel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-results
	MsgTypeExecuteReply = "execute_reply"

	// MsgTypeExecuteInput is the "msg_type" of an "execute_input" websocket
	// message on the "iopub" channel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-results
	MsgTypeExecuteInput = "execute_input"

	// MsgTypeExecuteResult is the "msg_type" of an "execute_result" websocket
	// message on the "iopub" channel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-results
	MsgTypeExecuteResult = "execute_result"

	// MsgTypeStream is the "msg_type" of a "stream" websocket message on the
	// "iopub" channel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#streams-stdout-stderr-etc
	MsgTypeStream = "stream"

	// MsgTypeKernelInfoRequest is the "msg_type" of a "kernel_info_request"
	// websocket message on the "shell" channel. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#kernel-info
	MsgTypeKernelInfoRequest = "kernel_info_request"

	// ExecuteReplyStatusOk is the "ok" status in the "content" of an
	// "execute_reply" websocket message. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-results
	ExecuteReplyStatusOk = "ok"

	// ExecuteReplyStatusError is the "error" status in the "content" of an
	// "execute_reply" websocket message. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-results
	ExecuteReplyStatusError = "error"

	// ExecuteReplyStatusAborted is the "aborted" status in the "content" of an
	// "execute_reply" websocket message. See
	// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-results
	ExecuteReplyStatusAborted = "aborted"
)

// Kernel represents a Jupyter kernel in the jupyter-server REST API.
// See https://jupyter-server.readthedocs.io/en/latest/developers/rest-api.html#get--api-kernels-kernel_id
// (this is not documented well)
type Kernel struct {
	ID             string `json:"id,omitempty"`
	Name           string `json:"name,omitempty"`
	LastActivity   string `json:"last_activity,omitempty"`
	ExecutionState string `json:"execution_state,omitempty"`
	Connections    *int   `json:"connections,omitempty"`
}

// Session represents a Jupyter session in the jupyter-server REST API.
// See https://jupyter-server.readthedocs.io/en/latest/developers/rest-api.html#get--api-sessions-session
type Session struct {
	ID     string `json:"id,omitempty"`
	Name   string `json:"name"`
	Path   string `json:"path"`
	Type   string `json:"type"`
	Kernel Kernel `json:"kernel"`
}

// ErrorMsg is the format of error messages returned by the jupyter-server REST
// API.
type ErrorMsg struct {
	Message string `json:"message,omitempty"`
	Reason  string `json:"reason,omitempty"`
}

// WebsocketMsgHeader is the header of a Jupyter websocket message.
// See https://jupyter-client.readthedocs.io/en/latest/messaging.html#message-header
type WebsocketMsgHeader struct {
	MsgID    string `json:"msg_id,omitempty"`
	MsgType  string `json:"msg_type,omitempty"`
	Session  string `json:"session,omitempty"`
	Username string `json:"username,omitempty"`
	Date     string `json:"date,omitempty"`
	Version  string `json:"version,omitempty"`
}

// WebsocketMsgContent is the common interface for all the WebsocketMsg*Content
// types below. See WebsocketMsg.
type WebsocketMsgContent interface {
	// isContent is a dummy method that does not do anything.
	isContent()
}

// WebsocketMsgStatusContent is the "content" of a websocket message with
// "msg_type": "status" on the "iopub" channel. See
// https://jupyter-client.readthedocs.io/en/latest/messaging.html#status
type WebsocketMsgStatusContent struct {
	ExecutionState string `json:"execution_state,omitempty"`
}

func (c WebsocketMsgStatusContent) isContent() {}

// WebsocketMsgErrorContent is the "content" of a websocket message with
// "msg_type": "error" on the "iopub" channel. See
// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-errors
type WebsocketMsgErrorContent struct {
	Ename     string   `json:"ename,omitempty"`
	Evalue    string   `json:"evalue,omitempty"`
	Traceback []string `json:"traceback,omitempty"`
}

func (c WebsocketMsgErrorContent) isContent() {}

// WebsocketMsgExecuteRequestContent is the "content" of a websocket message
// with "msg_type": "execute_request" on the "shell" channel. See
// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execute
type WebsocketMsgExecuteRequestContent struct {
	Code            string         `json:"code,omitempty"`
	Silent          *bool          `json:"silent,omitempty"`
	StoreHistory    *bool          `json:"store_history,omitempty"`
	UserExpressions map[string]any `json:"user_expressions"`
	AllowStdin      *bool          `json:"allow_stdin,omitempty"`
	StopOnError     *bool          `json:"stop_on_error,omitempty"`
}

func (c WebsocketMsgExecuteRequestContent) isContent() {}

// WebsocketMsgExecuteReplyContent is the "content" of a websocket message with
// "msg_type": "execute_reply" on the "shell" channel. See
// https://jupyter-client.readthedocs.io/en/latest/messaging.html#execution-results
type WebsocketMsgExecuteReplyContent struct {
	Status         string `json:"status,omitempty"`
	ExecutionCount *int   `json:"execution_count,omitempty"`

	// If the status is "ok", the following fields can be set.
	Payload         []map[string]any `json:"payload"`
	UserExpressions map[string]any   `json:"user_expressions"`

	// If the status is "error", the following fields can be set.
	Ename     string   `json:"ename,omitempty"`
	Evalue    string   `json:"evalue,omitempty"`
	Traceback []string `json:"traceback,omitempty"`
}

func (c WebsocketMsgExecuteReplyContent) isContent() {}

// WebsocketMsgExecuteInputContent is the "content" of a websocket message with
// "msg_type": "execute_input" on the "iopub" channel. See
// https://jupyter-client.readthedocs.io/en/latest/messaging.html#code-inputs
type WebsocketMsgExecuteInputContent struct {
	Code           string `json:"code,omitempty"`
	ExecutionCount *int   `json:"execution_count,omitempty"`
}

func (c WebsocketMsgExecuteInputContent) isContent() {}

// WebsocketMsgExecuteResultData is the "data" field of the "content" of a
// websocket message with "msg_type": "execute_result" on the "iopub" channel.
// This can contain more mime-type fields but we only need "text/plain" for now.
// See https://jupyter-client.readthedocs.io/en/latest/messaging.html#id6
type WebsocketMsgExecuteResultData struct {
	TextPlain string `json:"text/plain,omitempty"`
}

// WebsocketMsgExecuteResultContent is the "content" of a websocket message with
// "msg_type": "execute_result" on the "iopub" channel. See
// https://jupyter-client.readthedocs.io/en/latest/messaging.html#id6
type WebsocketMsgExecuteResultContent struct {
	Data           WebsocketMsgExecuteResultData `json:"data"`
	Metadata       map[string]any                `json:"metadata"`
	ExecutionCount *int                          `json:"execution_count,omitempty"`
}

func (c WebsocketMsgExecuteResultContent) isContent() {}

// WebsocketMsgStreamContent is the "content" of a websocket message with
// "msg_type": "stream" on the "iopub" channel. See
// https://jupyter-client.readthedocs.io/en/latest/messaging.html#streams-stdout-stderr-etc
type WebsocketMsgStreamContent struct {
	Name string `json:"name,omitempty"`
	Text string `json:"text,omitempty"`
}

func (c WebsocketMsgStreamContent) isContent() {}

// WebsocketMsg represents a Jupyter websocket message. See
// https://jupyter-client.readthedocs.io/en/latest/messaging.html#general-message-format
type WebsocketMsg struct {
	Channel      string             `json:"channel,omitempty"`
	Header       WebsocketMsgHeader `json:"header"`
	ParentHeader WebsocketMsgHeader `json:"parent_header"`
	Metadata     map[string]any     `json:"metadata"`
	// Content is an instance of the appropriate WebsocketMsg*Content type, e.g.:
	//   - WebsocketMsgStatusContent if Header.MsgType == "status" and Channel ==
	//     "iopub"
	//   - WebsocketMsgExecuteRequestContent if Header.MsgType ==
	//     "execute_request" and Channel == "shell"
	//   - ...
	// When unmarshalling a WebsocketMsg from JSON, an appropriate
	// WebsocketMsg*Content type is chosen automatically and you can use a type
	// switch over Content to check which message type you got.
	Content WebsocketMsgContent `json:"content"`
	Buffers []any               `json:"buffers"`
}

// UnmarshalJSON unmarshals a WebsocketMsg by parsing the "content" field based
// on the "msg_type" set in the message header.
func (m *WebsocketMsg) UnmarshalJSON(data []byte) error {
	// Unmarshal into a helper struct which is almost identical to WebsocketMsg
	// except that 'Content' is a map[string]any. We cannot use WebsocketMsg here
	// since that would cause an infinite recursion.
	var msgWithMapContent struct {
		Channel      string             `json:"channel,omitempty"`
		Header       WebsocketMsgHeader `json:"header"`
		ParentHeader WebsocketMsgHeader `json:"parent_header"`
		Metadata     map[string]any     `json:"metadata"`
		Content      map[string]any     `json:"content"`
		Buffers      []any              `json:"buffers"`
	}
	if err := json.Unmarshal(data, &msgWithMapContent); err != nil {
		return err
	}

	// Copy everything but 'Content'.
	m.Channel = msgWithMapContent.Channel
	m.Header = msgWithMapContent.Header
	m.ParentHeader = msgWithMapContent.ParentHeader
	m.Metadata = msgWithMapContent.Metadata
	m.Buffers = msgWithMapContent.Buffers

	var expectedChannel string
	switch m.Header.MsgType {
	case MsgTypeStatus:
		expectedChannel = ChannelIopub
	case MsgTypeError:
		expectedChannel = ChannelIopub
	case MsgTypeExecuteRequest:
		expectedChannel = ChannelShell
	case MsgTypeExecuteReply:
		expectedChannel = ChannelShell
	case MsgTypeExecuteInput:
		expectedChannel = ChannelIopub
	case MsgTypeExecuteResult:
		expectedChannel = ChannelIopub
	case MsgTypeStream:
		expectedChannel = ChannelIopub
	default:
		return fmt.Errorf("unknown channel for msg_type: %s", m.Header.MsgType)
	}

	if m.Channel != expectedChannel {
		return fmt.Errorf(
			"message with type '%s' has channel '%s', expected channel '%s'",
			m.Header.MsgType, m.Channel, expectedChannel,
		)
	}

	// Re-marshal the untyped content to bytes and then re-unmarshal into
	// the appropriate type based on the "msg_type" from the message header.
	contentBytes, err := json.Marshal(msgWithMapContent.Content)
	if err != nil {
		return fmt.Errorf("could not marshal content: %w", err)
	}

	switch m.Header.MsgType {
	case MsgTypeStatus:
		m.Content, err = unmarshalContent[WebsocketMsgStatusContent](contentBytes)
	case MsgTypeError:
		m.Content, err = unmarshalContent[WebsocketMsgErrorContent](contentBytes)
	case MsgTypeExecuteRequest:
		m.Content, err = unmarshalContent[WebsocketMsgExecuteRequestContent](contentBytes)
	case MsgTypeExecuteReply:
		m.Content, err = unmarshalContent[WebsocketMsgExecuteReplyContent](contentBytes)
	case MsgTypeStream:
		m.Content, err = unmarshalContent[WebsocketMsgStreamContent](contentBytes)
	case MsgTypeExecuteInput:
		m.Content, err = unmarshalContent[WebsocketMsgExecuteInputContent](contentBytes)
	case MsgTypeExecuteResult:
		m.Content, err = unmarshalContent[WebsocketMsgExecuteResultContent](contentBytes)
	default:
		return fmt.Errorf("unsupported msg_type: %s", m.Header.MsgType)
	}

	return err
}

func unmarshalContent[T WebsocketMsgContent](contentBytes []byte) (T, error) {
	var content T
	if err := json.Unmarshal(contentBytes, &content); err != nil {
		var empty T
		return empty, fmt.Errorf("unmarshallig content to %T: %w", empty, err)
	}
	return content, nil
}

// BoolPtr returns a pointer to the given bool value which allows inline
// construction of structs containing bool pointers.
func BoolPtr(value bool) *bool {
	return &value
}

// IntPtr returns a pointer to the given int value which allows inline
// construction of structs containing int pointers.
func IntPtr(value int) *int {
	return &value
}
