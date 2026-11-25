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

// Package jupyterclient implements the handlers for communicating with the
// Jupyter server
package jupyterclient

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"regexp"
	"slices"
	"strings"

	"intrinsic/executive/code/jupyter"
	"intrinsic/util/status/extstatus"

	log "github.com/golang/glog"
	"github.com/pborman/uuid"

	statuspb "google.golang.org/genproto/googleapis/rpc/status"
)

const (
	// ExtendedStatusComponent is the component for all ExtendedStatus messages in
	// this service.
	ExtendedStatusComponent = "ai.intrinsic.code_execution"
	// ErrorCodePythonError is the error code for a PythonError extended status
	// message.
	ErrorCodePythonError = 10000
	// ErrorTitlePythonError is the title for ErrorCodePythonError.
	ErrorTitlePythonError = "Python error"
)

// Client handles communication with a Jupyter server for a specific worker.
// Callers must close the websocket connection and call DeleteSession to clean
// up the session when done.
type Client struct {
	jupyterServerHost string
	workerIndex       int
	sessionID         string
	kernelID          string
	wsClient          *webSocketClient
}

// New creates a new Jupyter Client for the given server host and worker index.
func New(jupyterServerHost string, workerIndex int) *Client {
	return &Client{
		jupyterServerHost: jupyterServerHost,
		workerIndex:       workerIndex,
	}
}

// postJSON performs an HTTP POST request with JSON request and response bodies.
func postJSON(url url.URL, request any, response any, expectedCodes ...int) error {
	var requestBuffer bytes.Buffer
	err := json.NewEncoder(&requestBuffer).Encode(request)
	if err != nil {
		return fmt.Errorf("encoding post request to json: %w", err)
	}

	httpResponse, err := http.Post(url.String(), "application/json", &requestBuffer)
	if err != nil {
		return fmt.Errorf("making http post request: %w", err)
	}
	defer httpResponse.Body.Close()

	if !slices.Contains(expectedCodes, httpResponse.StatusCode) {
		body, err := io.ReadAll(httpResponse.Body)
		if err != nil {
			return fmt.Errorf(
				"reading post response body with unexpected response status '%s': %w",
				httpResponse.Status, err,
			)
		}
		return fmt.Errorf(
			"unexpected response status '%s' (response body: %v)",
			httpResponse.Status, string(body),
		)
	}

	err = json.NewDecoder(httpResponse.Body).Decode(response)
	if err != nil {
		return fmt.Errorf("decoding post response from json: %w", err)
	}

	return nil
}

// CreateSession creates a session on the jupyter server.
func (c *Client) CreateSession(ctx context.Context) error {
	err := c.createJupyterSession()
	if err != nil {
		return err
	}

	err = c.ensureActiveWebSocketConnection(ctx)
	if err != nil {
		_ = c.DeleteSession(ctx)
		return err
	}

	log.InfoContextf(ctx, "Worker %d: Created session %s with kernel %s", c.workerIndex, c.sessionID, c.kernelID)

	return nil
}

func (c *Client) createJupyterSession() error {
	postSessionsRequest := jupyter.Session{
		// A frontend such as JupyterLab would typically create a session such as
		// {"path": "sum.ipynb", "name": "sum.ipynb", "type": "notebook", ...}.
		// For our purpose it is only relevant that we use a unique "path" name
		// which will always create a new session (and kernel). Session "name" and
		// "type" seem to be irrelevant for this so we leave them empty.
		Path:   uuid.New(),
		Name:   "",
		Type:   "",
		Kernel: jupyter.Kernel{Name: jupyter.Python3KernelName},
	}

	sessionsURL := url.URL{Scheme: "http", Host: c.jupyterServerHost, Path: "api/sessions"}

	var session jupyter.Session
	if err := postJSON(sessionsURL, postSessionsRequest, &session, http.StatusCreated); err != nil {
		return fmt.Errorf("Worker %d: creating session: %w", c.workerIndex, err)
	}

	c.sessionID = session.ID
	c.kernelID = session.Kernel.ID
	return nil
}

// DeleteSession deletes the active session from the server.
func (c *Client) DeleteSession(ctx context.Context) error {
	if c.sessionID == "" {
		return nil
	}

	if c.wsClient != nil {
		if err := c.wsClient.close(); err != nil {
			log.ErrorContextf(ctx, "Worker %d: Failed closing websocket client: %v", c.workerIndex, err)
		}
		c.wsClient = nil
	}

	sessionsURL := url.URL{
		Scheme: "http",
		Host:   c.jupyterServerHost,
		Path:   fmt.Sprintf("api/sessions/%s", c.sessionID),
	}

	deleteRequest, err := http.NewRequestWithContext(ctx, "DELETE", sessionsURL.String(), nil)
	if err != nil {
		return fmt.Errorf("Worker %d: creating delete request: %w", c.workerIndex, err)
	}

	client := &http.Client{}
	response, err := client.Do(deleteRequest)
	if err != nil {
		return fmt.Errorf("Worker %d: sending delete request: %w", c.workerIndex, err)
	}
	defer response.Body.Close()

	if response.StatusCode != http.StatusNoContent {
		return fmt.Errorf("Worker %d: delete request returned unexpected status %s", c.workerIndex, response.Status)
	}

	log.InfoContextf(ctx, "Worker %d: Deleted session %s", c.workerIndex, c.sessionID)

	c.sessionID = ""
	c.kernelID = ""

	return nil
}

// Regex for ANSI escape sequences from https://github.com/acarl005/stripansi
const ansi = "[\u001B\u009B][[\\]()#;?]*(?:(?:(?:[a-zA-Z\\d]*(?:;[a-zA-Z\\d]*)*)?\u0007)|(?:(?:\\d{1,4}(?:;\\d{0,4})*)?[\\dA-PRZcf-ntqry=><~]))"

var ansiRegexp = regexp.MustCompile(ansi)

func removeANSIEscapeSequences(str string) string {
	return ansiRegexp.ReplaceAllString(str, "")
}

func sanitizeTraceback(traceback []string) string {
	var lines []string
	for _, line := range traceback {
		// The tracebacks from the Jupyter server contain a lot of ANSI escape
		// sequences (e.g. colors) which we remove here.
		lines = append(lines, removeANSIEscapeSequences(line))
	}
	return strings.Join(lines, "\n")
}

// PythonError represents a Python error which occurred on the Jupyter server.
type PythonError struct {
	// name is the name of the Python exception (e.g. "ValueError").
	name string
	// value is the message of the Python exception (e.g. "division by zero").
	value string
	// traceback is the Python stack trace as a multi-line string.
	traceback string
}

// ToStatus converts the PythonError to a Status proto with an ExtendedStatus.
func (e *PythonError) ToStatus() *statuspb.Status {
	extendedStatus := extstatus.New(ExtendedStatusComponent, ErrorCodePythonError,
		extstatus.WithTitle(ErrorTitlePythonError),
		extstatus.WithUserMessage(fmt.Sprintf("%s: %s", e.name, e.value)),
		extstatus.WithDebugMessage(e.traceback),
	)
	return extendedStatus.GRPCStatus().Proto()
}

func (e *PythonError) Error() string {
	return fmt.Sprintf(
		"%s: %s: %s\nTraceback:\n%s",
		ErrorTitlePythonError, e.name, e.value, e.traceback,
	)
}

func (c *Client) ensureActiveWebSocketConnection(ctx context.Context) error {
	if c.wsClient != nil && c.wsClient.isConnected() {
		return nil
	}

	log.InfoContextf(ctx, "Worker %d: WebSocket disconnected or not initialized, reconnecting...", c.workerIndex)
	if c.wsClient != nil {
		_ = c.wsClient.close()
		c.wsClient = nil
	}

	wsClient, err := newWebSocketClient(c.jupyterServerHost, c.kernelID, c.sessionID)
	if err != nil {
		return fmt.Errorf("Worker %d: connecting to websocket: %w", c.workerIndex, err)
	}
	c.wsClient = wsClient
	return nil
}

// ExecuteCode executes the given code in the active Jupyter
// session and kernel and returns exactly one of {execute result, Python error
// details, Go error}. If there is no execute result, returns ("", nil).
func (c *Client) ExecuteCode(ctx context.Context, code string, stdout chan<- string) (string, error) {
	if c.sessionID == "" || c.kernelID == "" {
		return "", fmt.Errorf("Worker %d: no active Jupyter session", c.workerIndex)
	}

	if err := c.ensureActiveWebSocketConnection(ctx); err != nil {
		return "", err
	}

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	log.InfoContextf(ctx, "Worker %d: Sending execute_request", c.workerIndex)

	receivedMessages := make(chan *jupyter.WebsocketMsg, 10)
	receivedMessagesErr := make(chan error, 1)

	if err := c.wsClient.sendExecuteCodeRequest(ctx, code, receivedMessages, receivedMessagesErr); err != nil {
		return "", fmt.Errorf("Worker %d: sending execute_request message: %w", c.workerIndex, err)
	}

	seenStatusOk := false
	seenStateIdle := false
	for {
		select {
		case msg := <-receivedMessages:
			// After the execution we watch for the following messages types:
			//  - optional "execute_result" with the execution result as content
			//    (only sent if the last statement of the executed code did not
			//    evaluate to None)
			//  - "status" with state "idle" (no other relevant content)
			//  - "execute_reply" with status "ok" (no other relevant content) or
			//    with status "error" (with Python error details in content)
			// The "execute_result" and "status" messages are sent on the same
			// channel ("iopub") and thus are guaranteed to arrive in this order.
			// The "execute_reply" message is sent on a different channel ("shell")
			// and may arrive first, last or in between. The absence of an
			// "execute_result" message is only certain after we have seen the
			// "status" ("idle") message.
			switch content := msg.Content.(type) {
			case jupyter.WebsocketMsgStreamContent:
				// Stream messages are messages emitted to stdout during execution.
				// They contain things like manually written print statements from the
				// user. We forward all of them to the provided channel (if one is
				// set). The caller can then use these messages if needed.
				if len(content.Text) < 100 {
					log.InfoContextf(ctx, "Worker %d: Received stream message: %s", c.workerIndex, content.Text)
				} else {
					log.InfoContextf(ctx, "Worker %d: Received stream message: %s...", c.workerIndex, content.Text[:100])
				}

				if stdout != nil {
					stdout <- content.Text
				}
			case jupyter.WebsocketMsgExecuteResultContent:
				// We received an "execute_result" which implies that the execution was
				// successful. Return the result.
				executeResult := content.Data.TextPlain

				if len(executeResult) < 100 {
					log.InfoContextf(ctx, "Worker %d: Received execute_result: %v", c.workerIndex, executeResult)
				} else {
					log.InfoContextf(ctx, "Worker %d: Received execute_result: %v...", c.workerIndex, executeResult[:100])
				}

				return executeResult, nil
			case jupyter.WebsocketMsgExecuteReplyContent:
				if content.Status == jupyter.ExecuteReplyStatusError {
					// We received an "execute_reply" with status "error" which implies
					// that the execution failed and there won't be an
					// "execute_result".
					log.InfoContextf(ctx, "Worker %d: Received Python error: %s: %s", c.workerIndex, content.Ename, content.Evalue)
					return "", &PythonError{
						name:      content.Ename,
						value:     content.Evalue,
						traceback: sanitizeTraceback(content.Traceback),
					}
				}
				if content.Status == jupyter.ExecuteReplyStatusOk {
					// We received an "execute_reply" with status "ok" but we have
					// not received an "execute_result" yet.
					if seenStateIdle {
						// We have seen the "status" ("idle") message, so the execution
						// was successful but had no execute result.
						log.InfoContextf(ctx, "Worker %d: Received status 'ok' without execute_result", c.workerIndex)
						return "", nil
					}
					// Continue waiting, there might still be an "execute_result".
					seenStatusOk = true
				}
			case jupyter.WebsocketMsgStatusContent:
				if content.ExecutionState == jupyter.ExecutionStateIdle {
					// We have received a "status" ("idle") message but we have not
					// received an "execute_result", i.e., there is no execute result.
					if seenStatusOk {
						// We have seen an "execute_reply" with status "ok", so the execution
						// was successful but had no execute result.
						log.InfoContextf(ctx, "Worker %d: Received status 'ok' without execute_result", c.workerIndex)
						return "", nil
					}
					// Continue waiting for the "execute_reply" to see if the execution
					// was successful or resulted in an error.
					seenStateIdle = true
				}
			}
		case err := <-receivedMessagesErr:
			return "", fmt.Errorf("Worker %d: receiving messages: %w", c.workerIndex, err)
		case <-ctx.Done():
			return "", ctx.Err()
		}
	}
}
