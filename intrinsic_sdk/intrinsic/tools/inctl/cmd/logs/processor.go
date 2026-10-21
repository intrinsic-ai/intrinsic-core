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

package logs

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"math/rand"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"path"
	"reflect"
	"strconv"
	"strings"
	"sync"
	"time"

	"intrinsic/skills/tools/skill/cmd/dialerutil"
	"intrinsic/skills/tools/skill/cmd/solutionutil"
	"intrinsic/tools/inctl/auth/auth"

	backoff "github.com/cenkalti/backoff/v4"
)

const (
	paramSkillID    = "skillID"
	paramResourceID = "resourceName"
	paramFollow     = "follow"
	paramTimestamps = "timestamps"
	paramTailLines  = "tailLines"
	paramSinceSec   = "sinceSeconds"

	headerRetryAfter = "Retry-After"
)

// TODO: For now this is mostly copy of skill logs, but there is an idea here for future. b/335768409

const (
	localhostURL = "localhost:17080"
)

var (
	verboseDebug           = false
	verboseOut   io.Writer = os.Stderr
	httpClient             = &http.Client{
		Timeout:   15 * time.Second,
		Transport: http.DefaultTransport,
	}
	stdoutMutex = &sync.Mutex{}
)

type endpoint struct {
	url           *url.URL
	authToken     *auth.ProjectToken
	xsrfTokenFunc func(ctx context.Context, params *cmdParams) (string, error)
}

func getClusterName(ctx context.Context, params *cmdParams) (string, error) {
	serverAddr := fmt.Sprintf("dns:///www.endpoints.%s.cloud.goog:443", params.projectName)
	ctx, conn, err := dialerutil.DialConnectionCtx(ctx, dialerutil.DialInfoParams{
		Address:  serverAddr,
		CredName: params.projectName,
		CredOrg:  params.org,
	})
	if err != nil {
		return "", fmt.Errorf("could not create connection: %v", err)
	}
	defer conn.Close()

	cluster, err := solutionutil.GetClusterNameFromSolutionOrDefault(
		ctx,
		conn,
		params.solution,
		params.context,
	)
	if err != nil {
		return "", fmt.Errorf("could not resolve solution to cluster: %s", err)
	}
	return cluster, nil
}

// createEndpoint creates an endpoint for fetching logs. The endpoint contains the URL, XSRF token,
// and auth token required to fetch logs. The endpoint is different depending on whether the user
// is fetching logs from a local minikube route, a local onprem cluster via local IP route, or workcells/VMs via a cloud route.
func createEndpoint(ctx context.Context, params *cmdParams) (*endpoint, error) {
	var xsrfTokenURL *url.URL
	var authToken *auth.ProjectToken
	var localAddress string
	var clusterName string

	if params.context == "minikube" { // minikube local route
		localAddress = localhostURL
	} else if params.onpremAddress != "" { // onprem local route
		localAddress = params.onpremAddress
	} else { // cloud route
		var err error
		if params.context != "" { // if context is set, use it as the cluster name
			clusterName = params.context
		} else {
			clusterName, err = getClusterName(ctx, params)
			if err != nil {
				return nil, fmt.Errorf("could not resolve solution to cluster: %w", err)
			}
		}
	}

	if localAddress == "" {
		xsrfTokenURL = &url.URL{
			Host:   fmt.Sprintf("www.endpoints.%s.cloud.goog", params.projectName),
			Path:   fmt.Sprintf("frontend/client/%s/api/token", clusterName),
			Scheme: "https",
		}
		var err error
		authToken, err = getAuthToken(params.projectName)
		if err != nil {
			return nil, err
		}
	} else {
		xsrfTokenURL = &url.URL{
			Host:   localAddress,
			Path:   "frontend/api/token",
			Scheme: "http",
		}
	}

	var logsURL *url.URL
	if localAddress != "" {
		logsURL = &url.URL{
			Host:   localAddress,
			Path:   "frontend/api/consoleLogs",
			Scheme: "http",
		}
	} else {
		logsURL = &url.URL{
			Host:   fmt.Sprintf("www.endpoints.%s.cloud.goog", params.projectName),
			Path:   fmt.Sprintf("frontend/client/%s/api/consoleLogs", clusterName),
			Scheme: "https",
		}
	}

	return &endpoint{
		url:       logsURL,
		authToken: authToken,
		xsrfTokenFunc: func(ctx context.Context, params *cmdParams) (string, error) {
			return callEndpoint(ctx, http.MethodGet, xsrfTokenURL, authToken, nil, nil,
				func(_ context.Context, body io.Reader) (string, error) {
					token, err := io.ReadAll(body)
					return string(token), err
				})
		},
	}, nil
}

type bodyReader = func(context.Context, io.Reader) (string, error)

type resourceType int

const (
	maxConnectionRetries = 1024 // this is humongous number, but reasonable upper bound in case things go VERY wrong.

	rtService resourceType = iota
	rtSkill
	rtAsset
)

type cmdParams struct {
	resourceType  resourceType
	resourceID    string
	follow        bool
	timestamps    bool
	tailLines     int
	projectName   string
	sinceSeconds  string
	onpremAddress string
	context       string
	solution      string
	org           string
	prefixID      bool
	prefixType    bool
}

func buildPrefix(params *cmdParams) string {
	if !params.prefixType && !params.prefixID {
		return ""
	}
	var parts []string
	if params.prefixType {
		switch params.resourceType {
		case rtSkill:
			parts = append(parts, "Skill")
		case rtService:
			parts = append(parts, "Service")
		case rtAsset:
			parts = append(parts, "Asset")
		}
	}
	if params.prefixID {
		parts = append(parts, params.resourceID)
	}

	return fmt.Sprintf("[%s] ", strings.Join(parts, ": "))
}

func readLogsFromSolution(ctx context.Context, params *cmdParams, w io.Writer) error {
	endpoint, err := createEndpoint(ctx, params)
	if err != nil {
		return err
	}

	consoleLogsURL := endpoint.url
	consoleLogsURL.Path = path.Join(consoleLogsURL.EscapedPath(), "consoleLogs")
	consoleLogsQuery := setResourceID(params.resourceType, params.resourceID)
	if params.follow {
		consoleLogsQuery.Set(paramFollow, fmt.Sprintf("%t", params.follow))
	} else {
		consoleLogsQuery.Set(paramTailLines, fmt.Sprintf("%d", params.tailLines))
	}
	consoleLogsQuery.Set(paramTimestamps, fmt.Sprintf("%t", params.timestamps))

	if d, ok, err := parseSinceSeconds(params.sinceSeconds); ok && err == nil {
		// nit: our now is different from server now (at the time of processing),
		// so we can get drift of a second give or take
		// this is not generally problematic for this kind of logs.
		// To avoid this in the future, server should accept full timestamp, not duration
		sinceSeconds := fmt.Sprintf("%d", int64(d.Truncate(time.Second).Seconds()))
		consoleLogsQuery.Set(paramSinceSec, sinceSeconds)
	} else if err != nil {
		return fmt.Errorf("cannot parse parameter --%s: %w", keySinceSec, err)
	}

	consoleLogsURL.RawQuery = consoleLogsQuery.Encode()
	notify := clientNotify(w)
	backOff := backoff.NewExponentialBackOff()
	backOff.MaxElapsedTime = 10 * time.Minute // if we were not able to obtain response in 10 minutes, we should give up.
	var xsrfToken string
	for reconnectCount := 0; ctx.Err() == nil && reconnectCount < maxConnectionRetries; reconnectCount++ {
		err = backoff.RetryNotify(func() error {
			xsrfToken, err = endpoint.xsrfTokenFunc(ctx, params)
			if err != nil && shouldTerminate(err) {
				return backoff.Permanent(err)
			}
			if rat, ok := err.(*tooManyRequestsErr); ok {
				// we are going to forcibly wait to ensure we do not hammer server unnecessarily
				// this will stack with next backoff, but we don't have good way to skip next backoff
				select {
				case <-ctx.Done():
					return backoff.Permanent(ctx.Err())
				case <-time.After(rat.retryAfter):
					return rat
				}
			}
			return err
		}, backOff, notify)
		if err != nil {
			return fmt.Errorf("cannot obtain XSRF token: %w", err)
		}

		xsrfHeader := http.Header{"X-XSRF-TOKEN": []string{xsrfToken}}

		_, err = callEndpoint(ctx, http.MethodGet, consoleLogsURL, endpoint.authToken, xsrfHeader, nil,
			func(_ context.Context, body io.Reader) (string, error) {
				prefix := buildPrefix(params)
				scanner := bufio.NewScanner(body)
				for scanner.Scan() {
					backOff.Reset() // successfully read a line, reset exponential backoff
					stdoutMutex.Lock()
					fmt.Fprintf(w, "%s%s\n", prefix, scanner.Text())
					stdoutMutex.Unlock()
				}
				if err := scanner.Err(); err != nil {
					return "", err
				}
				return "", nil
			})

		if err == nil {
			return nil // We are done here, we received EOF from server.
		}

		if shouldTerminate(err) {
			return fmt.Errorf("terminal error for target '%s': %w", params.resourceID, err)
		}

		// adding arbitrary wait of up to a second to ensure we don't stomp the server
		arbitraryWait := time.Duration(500+rand.Int63n(501)) * time.Millisecond
		notify(err, arbitraryWait)
		select {
		case <-time.After(arbitraryWait):
			continue
		case <-ctx.Done():
			return nil
		}
	}
	return nil
}

func shouldTerminate(err error) bool {
	if _, ok := err.(*tooManyRequestsErr); ok {
		return false
	}
	if se, ok := err.(*statusErr); ok {
		// we are going to terminate if we get status below 500.
		// Status 500+ indicates that we could have some issue in relay, thus we retry
		return se.httpCode < 500 || se.httpCode == http.StatusNotImplemented
	}

	if errors.Is(err, context.Canceled) || errors.Is(err, io.EOF) {
		return true
	}

	if unwrapErr := errors.Unwrap(err); unwrapErr != nil {
		err = unwrapErr
	}

	if err != nil {
		if verboseDebug {
			fmt.Fprintf(verboseOut, "Type: %v; err: %s\n", reflect.TypeOf(err), err)
		}

		// Check if the error is a "resource not found" error.
		if strings.Contains(err.Error(), "resource not found") {
			return true
		}

		// Explicitly check for context deadline exceeded.
		if errors.Is(err, context.DeadlineExceeded) {
			return true
		}

		// Check if we have some sort of net/url error.
		if tErr, ok := err.(timeout); ok {
			return !tErr.Timeout()
		}
	}

	return false
}

func setResourceID(resType resourceType, id string) url.Values {
	result := make(url.Values)
	switch resType {
	case rtSkill:
		result.Add(paramSkillID, id)
	case rtService:
		result.Add(paramResourceID, id)
	case rtAsset:
	default:
	}
	return result
}

// callEndpoint calls given endpoint URL and handles all edge cases. If response is 200 OK
// and response body processing function (bodyFx) is present, response body is passed
// for processing. Otherwise, "", nil is return value.
func callEndpoint(ctx context.Context, method string, endpoint *url.URL, authToken *auth.ProjectToken, headers http.Header, payload io.Reader, bodyFx bodyReader) (string, error) {
	if verboseDebug {
		fmt.Fprintf(verboseOut, "URL: '%s'\n", endpoint)
	}

	req, err := http.NewRequestWithContext(ctx, method, endpoint.String(), payload)
	if len(headers) > 0 {
		req.Header = headers
	}
	if err != nil {
		return "", fmt.Errorf("could not create request: %w", err)
	}

	if authToken != nil {
		req, err = authToken.HTTPAuthorization(req)
		if err != nil {
			return "", fmt.Errorf("cannot obtain credentials: %w", err)
		}
	}

	printRequest(req)
	response, err := httpClient.Do(req)
	if err != nil {
		return "", fmt.Errorf("request to target failed: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		printResponse(response)
		content, _ := io.ReadAll(response.Body)
		if response.StatusCode == http.StatusTooManyRequests {
			return "", retryAfter(response, string(content))
		}
		if response.StatusCode == http.StatusNotFound {
			return "", fmt.Errorf("resource not found: %s. Check if the target exists in your solution", endpoint.String())
		}
		return "", &statusErr{httpCode: response.StatusCode, extra: string(content)}
	}
	if bodyFx != nil {
		return bodyFx(ctx, response.Body)
	}

	// empty body consumer is valid
	return "", nil
}

// Prints request headers and body (if present) into std_err.
func printRequest(req *http.Request) {
	if !verboseDebug || req == nil {
		return
	}
	if out, err := httputil.DumpRequest(req, true); err == nil {
		fmt.Fprintln(verboseOut, "-- REQUEST ------------------------------------------")
		fmt.Fprintln(verboseOut, string(out))
		fmt.Fprintln(verboseOut, "-----------------------------------------------------")
	} else {
		fmt.Fprintf(verboseOut, "cannot print request: %s\n", err)
	}
}

// Prints response headers and body (if present) into std_err.
func printResponse(res *http.Response) {
	if !verboseDebug || res == nil {
		return
	}
	if out, err := httputil.DumpResponse(res, true); err == nil {
		fmt.Fprintln(verboseOut, "-- RESPONSE -----------------------------------------")
		fmt.Fprintln(verboseOut, string(out))
		fmt.Fprintln(verboseOut, "-----------------------------------------------------")
	} else {
		fmt.Fprintf(verboseOut, "cannot print response: %s\n", err)
	}
}

// parseSinceSeconds implements manual handling of duration parsing in order to allow
// user to specify relative duration or use RFC3339 datum format.
func parseSinceSeconds(since string) (time.Duration, bool, error) {
	if since == "" {
		return 0, false, nil
	}
	// let's try to parse duration, as that is more realistic
	if d, err := time.ParseDuration(since); err == nil {
		// duration accepts signed value, we ignore that as we cannot read logs from future
		if d < 0 {
			d = -d
		}
		return d, true, nil
	} else if verboseDebug {
		fmt.Fprintf(verboseOut, "failed to parse %s as duration (may not be an issue): %s", keySinceSec, err)
	}

	t, err := time.Parse(time.RFC3339, since)
	if err != nil {
		if verboseDebug {
			fmt.Fprintf(verboseOut, "failed to %s since as RFC-3339 time: %s", keySinceSec, err)
		}
		return 0, true, fmt.Errorf("cannot convert %s to duration", keySinceSec)

	}

	if t.After(time.Now()) {
		return 0, true, fmt.Errorf("time %s is in future, cannot proceed", keySinceSec)
	}
	return time.Now().Sub(t), true, nil
}

func getAuthToken(project string) (*auth.ProjectToken, error) {
	if project == "" {
		// No authorization required (e.g. local call in tests)
		return nil, nil
	}

	config, err := auth.NewStore().GetConfiguration(project)
	if err != nil {
		return nil, err
	}
	return config.GetDefaultCredentials()
}

type statusErr struct {
	httpCode int
	extra    string
}

func (s *statusErr) Error() string {
	return fmt.Sprintf("error reading from server: %s (%d) %s", http.StatusText(s.httpCode), s.httpCode, s.extra)
}

type tooManyRequestsErr struct {
	statusErr
	retryAfter time.Duration
}

func (tmr *tooManyRequestsErr) Error() string {
	return fmt.Sprintf("too many requests, retry in %dms", tmr.retryAfter.Milliseconds())
}

func retryAfter(response *http.Response, content string) error {
	retryAfterH := response.Header[headerRetryAfter]
	tmrErr := &tooManyRequestsErr{
		statusErr:  statusErr{httpCode: response.StatusCode, extra: content},
		retryAfter: 100 * time.Millisecond, // reasonable default
	}
	if len(retryAfterH) > 0 {
		// we don't care about errors really, so we are going to mostly ignore them.
		// this can have either time, or duration in seconds, let's try duration first
		atoi, err := strconv.Atoi(retryAfterH[0])
		if err == nil {
			tmrErr.retryAfter = time.Duration(atoi) * time.Second
		} else {
			// we fail to parse string as simple number, let's parse as time
			rat, err := http.ParseTime(retryAfterH[0])
			if err == nil {
				tmrErr.retryAfter = rat.Sub(time.Now())
			}
		}
	}
	return tmrErr
}

func clientNotify(w io.Writer) backoff.Notify {
	return func(err error, duration time.Duration) {
		var msg string
		if errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) {
			msg = "Connection closed by server"
		} else if statusErr, ok := err.(*statusErr); ok && statusErr.httpCode >= 500 {
			msg = fmt.Sprintf("Server proxy error (%d)", statusErr.httpCode)
		} else if tErr, ok := err.(timeout); ok && tErr.Timeout() {
			msg = "Network timeout"
		} else {
			msg = "Connection lost"
		}

		fmt.Fprintf(os.Stderr, "[inctl] %s, retrying in %dms...\n", msg, duration.Milliseconds())
		if verboseDebug {
			fmt.Fprintf(verboseOut, "Details: %s\n", err)
		}
	}
}

type timeout interface {
	Timeout() bool
}
