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

package main

import (
	"context"
	"flag"
	"fmt"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"intrinsic/kubernetes/intrinsic"
	"intrinsic/storage/onprem_cas/internal/gc"
	"intrinsic/storage/onprem_cas/internal/handlers"

	"log/slog"

	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
)

var (
	flagPort                 = flag.Int("port", 50051, "The gRPC port.")
	flagObjectsDir           = flag.String("objects_dir", "/tmp/onprem-cas", "Where the CAS objects are stored.")
	flagPartialDir           = flag.String("partial_dir", "/tmp/onprem-cas-partial", "Where the partial CAS objects are stored during upload.")
	flagSyncSetRetentionDays = flag.Int("syncset_retention_days", 30, "Age in days after which outdated completed SyncSets (and their long running operation results) are deleted from disk. Set to 0 to always delete older completed SyncSets immediately.")
)

// maxGCDuration specifies the maximum duration we run the garbage collection
// during startup. This is well above the required duration to delete files
// and is only there as a fallback should the goroutine get stuck.
const maxGCDuration = 10 * time.Minute

func mainImpl() error {
	slog.SetDefault(slog.New(slog.NewTextHandler(os.Stdout, nil)))
	intrinsic.Init()

	if err := os.MkdirAll(*flagObjectsDir, 0755); err != nil {
		return fmt.Errorf("failed to create objects directory %q: %w", *flagObjectsDir, err)
	}
	if err := os.MkdirAll(*flagPartialDir, 0755); err != nil {
		return fmt.Errorf("failed to create partial directory %q: %w", *flagPartialDir, err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	var wg sync.WaitGroup

	// Identify and delete corrupt objects synchronously before starting the server.
	corrupt, err := gc.IdentifyCorruptObjects(*flagObjectsDir)
	if err != nil {
		slog.Warn("Failed to identify corrupt objects for cleanup", slog.Any("error", err))
	} else if len(corrupt) > 0 {
		slog.Info("GC: Identified corrupt objects for startup cleanup", slog.Int("count", len(corrupt)))
		gcCtx, gcCancel := context.WithTimeout(ctx, maxGCDuration)
		if gcErr := gc.DeleteFiles(gcCtx, corrupt); gcErr != nil {
			slog.Error("Garbage collection of corrupt objects completed with error", slog.Any("error", gcErr))
		}
		gcCancel()
	}

	// Identify stale partial files synchronously before starting the server.
	// This avoids race conditions where a concurrent GC deletes newly created
	// partial files from fresh uploads.
	partials, err := gc.IdentifyPartialFiles(*flagPartialDir)
	if err != nil {
		slog.Warn("Failed to identify partial files for cleanup", slog.Any("error", err))
	}

	// Delete identified stale partial files in the background.
	if len(partials) > 0 {
		wg.Go(func() {
			gcCtx, gcCancel := context.WithTimeout(ctx, maxGCDuration)
			defer gcCancel()
			slog.Info("GC: Starting deletion of identified partial files", slog.Int("count", len(partials)))
			if gcErr := gc.DeleteFiles(gcCtx, partials); gcErr != nil {
				slog.Error("Garbage collection completed with error", slog.Any("error", gcErr))
			}
		})
	}

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *flagPort))
	if err != nil {
		return fmt.Errorf("failed to listen: %w", err)
	}

	slog.Info("Running in local-only mode. Sync API is disabled.")

	casHandler := handlers.NewLocalCASHandler(*flagObjectsDir, *flagPartialDir)

	grpcServer := grpc.NewServer(grpc.StatsHandler(new(ocgrpc.ServerHandler)))
	caspb.RegisterContentAddressableStorageServiceServer(grpcServer, casHandler)
	reflection.Register(grpcServer)

	slog.Info("Starting local-cas service", slog.Int("port", *flagPort))

	shutdown := make(chan os.Signal, 1)
	signal.Notify(shutdown, syscall.SIGTERM, syscall.SIGINT)

	serverError := make(chan error, 1)
	wg.Go(func() {
		if err := grpcServer.Serve(lis); err != nil {
			serverError <- err
		}
	})

	var serveErr error
	select {
	case err := <-serverError:
		slog.Error("gRPC server failed", slog.Any("error", err))
		serveErr = err
	case sig := <-shutdown:
		slog.Info("Received shutdown signal", slog.Any("signal", sig))
	}

	slog.Info("Shutting down...")
	cancel()
	grpcServer.GracefulStop()
	wg.Wait()
	return serveErr
}

func main() {
	if err := mainImpl(); err != nil {
		slog.Error("Fatal execution error", slog.Any("error", err))
		os.Exit(1)
	}
}
