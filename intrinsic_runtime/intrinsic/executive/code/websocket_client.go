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

package jupyterclient

import (
	"context"
	"errors"
	"fmt"
	"net/url"
	"sync"

	"intrinsic/executive/code/jupyter"

	"github.com/gorilla/websocket"
	"github.com/pborman/uuid"
)

// webSocketClient manages a websocket connection to a Jupyter kernel for
// bidirectional messaging.
//
// Usage:
//  1. Create a client with [newWebSocketClient], which dials the kernel
//     websocket endpoint and immediately starts a background reader goroutine.
//  2. Dispatch a code execution request via [webSocketClient.sendExecuteCodeRequest],
//     passing the code and response/error channels. Incoming messages matching
//     the execution are automatically forwarded to the receiver channel.
//     Only one execution request may be in-flight at a time; callers must not
//     call sendExecuteCodeRequest again until the previous execution has finished
//     (i.e., received the idle status message from Jupyter or ctx is canceled).
//  3. On shutdown make sure to call [webSocketClient.close] to close the websocket
//     connection and stop the background reader goroutine.
type webSocketClient struct {
	conn *websocket.Conn

	// mu protects all fields below: the active request receiver state and closed flag.
	mu sync.Mutex

	// The following fields represent the routing destination for incoming messages
	// corresponding to the active execution request. They must always be updated together
	// atomically under mu to maintain a consistent request routing state.
	msgCh     chan<- *jupyter.WebsocketMsg
	errCh     chan<- error
	requestID string
	reqCtx    context.Context
	// closed indicates whether the websocket connection has been closed. Guarded by mu.
	closed bool
}

// newWebSocketClient connects to the Jupyter kernel websocket and immediately
// starts a background goroutine for receiving messages.
func newWebSocketClient(serverHost, kernelID, sessionID string) (*webSocketClient, error) {
	websocketURL := url.URL{
		Scheme:   "ws",
		Host:     serverHost,
		Path:     fmt.Sprintf("api/kernels/%s/channels", kernelID),
		RawQuery: fmt.Sprintf("session_id=%s", sessionID),
	}

	conn, _, err := websocket.DefaultDialer.Dial(websocketURL.String(), nil)
	if err != nil {
		return nil, fmt.Errorf("connecting to websocket: %w", err)
	}

	client := &webSocketClient{
		conn: conn,
	}

	go client.receiveMessages()

	return client, nil
}

func (c *webSocketClient) receiveMessages() {
	defer func() {
		_ = c.close()
	}()

	for {
		msg := &jupyter.WebsocketMsg{}
		if err := c.conn.ReadJSON(msg); err != nil {
			c.mu.Lock()
			closed := c.closed
			errCh := c.errCh
			reqCtx := c.reqCtx
			c.msgCh = nil
			c.errCh = nil
			c.reqCtx = nil
			c.requestID = ""
			c.mu.Unlock()

			if closed || errCh == nil || reqCtx == nil || reqCtx.Err() != nil {
				return
			}
			select {
			case errCh <- fmt.Errorf("reading message: %w", err):
			case <-reqCtx.Done():
			}
			return
		}

		c.mu.Lock()
		msgCh := c.msgCh
		reqID := c.requestID
		reqCtx := c.reqCtx
		c.mu.Unlock()

		if msgCh == nil {
			continue
		}
		if reqID != "" && msg.ParentHeader.MsgID != reqID {
			continue
		}

		select {
		case msgCh <- msg:
		case <-reqCtx.Done():
		}
	}
}

// isConnected returns whether the websocket client connection is active and open.
func (c *webSocketClient) isConnected() bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	return !c.closed
}

// sendExecuteCodeRequest generates a unique request ID, registers the receiver
// channels, and sends an execute_request message to the Jupyter kernel.
//
// Only one request may be in-flight at a time. Callers must wait until the
// active execution finishes (upon receiving the idle status message or context
// cancellation) before dispatching another request.
func (c *webSocketClient) sendExecuteCodeRequest(
	ctx context.Context,
	code string,
	msgCh chan<- *jupyter.WebsocketMsg,
	errCh chan<- error,
) error {
	if ctx == nil {
		return errors.New("context cannot be nil")
	}

	requestID := uuid.New()

	executeRequest := jupyter.WebsocketMsg{
		Channel: jupyter.ChannelShell,
		Header: jupyter.WebsocketMsgHeader{
			MsgID:   requestID,
			MsgType: jupyter.MsgTypeExecuteRequest,
		},
		Content: jupyter.WebsocketMsgExecuteRequestContent{
			Silent: jupyter.BoolPtr(false),
			Code:   code,
		},
		Metadata: map[string]any{},
		Buffers:  []any{},
	}

	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return fmt.Errorf("websocket client is closed")
	}
	c.msgCh = msgCh
	c.errCh = errCh
	c.requestID = requestID
	c.reqCtx = ctx
	c.mu.Unlock()

	if err := c.conn.WriteJSON(executeRequest); err != nil {
		return fmt.Errorf("sending execute_request message: %w", err)
	}
	return nil
}

// close marks the client as closed, closes the underlying websocket connection,
// and terminates the background reader goroutine. Subsequent calls to close are no-ops.
func (c *webSocketClient) close() error {
	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return nil
	}
	c.closed = true
	c.mu.Unlock()

	return c.conn.Close()
}
