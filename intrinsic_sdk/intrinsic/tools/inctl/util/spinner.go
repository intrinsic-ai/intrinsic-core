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

package util

import (
	"context"
	"fmt"
	"io"
	"math"
	"time"

	"intrinsic/tools/inctl/util/color"
)

// hsvToRGB converts an HSV color (0-360, 0-1, 0-1) to an RGB color (0-255).
func hsvToRGB(h float64, s float64, v float64) (uint8, uint8, uint8) {
	c := v * s
	x := c * (1 - math.Abs(math.Mod(h/60.0, 2)-1))
	m := v - c

	var r, g, b float64
	switch {
	case h >= 0 && h < 60:
		r, g, b = c, x, 0
	case h >= 60 && h < 120:
		r, g, b = x, c, 0
	case h >= 120 && h < 180:
		r, g, b = 0, c, x
	case h >= 180 && h < 240:
		r, g, b = 0, x, c
	case h >= 240 && h < 300:
		r, g, b = x, 0, c
	case h >= 300 && h < 360:
		r, g, b = c, 0, x
	}

	return uint8((r + m) * 255), uint8((g + m) * 255), uint8((b + m) * 255)
}

// Spinner represents a simple text-based spinner for CLI output.
//
// Output Examples:
//
//	Start("Doing work...") prints:
//	  Doing work... ⠋
//
//	Interrupt("Warning: hit a snag, retrying...") clears the spinner, prints the warning, and redraws the spinner:
//	  Warning: hit a snag, retrying...
//	  Doing work... ⠙
//
//	Stop("Done!") clears the spinner and prints the final message:
//	  Warning: hit a snag, retrying...
//	  Done!
type Spinner struct {
	out          io.Writer
	frames       []string
	interval     time.Duration
	ctx          context.Context
	cancel       context.CancelFunc
	message      string
	lastPrintLen int
	frameIdx     int
	updateCh     chan string
	styleCh      chan SpinnerStyle
	doneCh       chan struct{}
	position     SpinnerPosition
	color        SpinnerColor
	reverse      bool
}

// SpinnerDirection represents the animation direction.
type SpinnerDirection int

const (
	DirectionForward SpinnerDirection = iota
	DirectionReverse
)

// SpinnerPosition represents whether the spinner is before or after the text.
type SpinnerPosition int

const (
	// PositionBack places the spinner after the text.
	PositionBack SpinnerPosition = iota
	// PositionFront places the spinner before the text.
	PositionFront
)

// SpinnerStyle defines the sequence of frames for a spinner animation.
type SpinnerStyle []string

var (
	// StyleDefault is the standard circular braille spinner.
	StyleSpinningDots = SpinnerStyle{"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"}
	// StyleFillUp represents an upward filling braille pattern.
	StyleFillUp = SpinnerStyle{" ", "⠤", "⠶", "⠿", "⠿", "⠿", "⠿"}
	// StyleWaveThrough represents an oscillating braille pattern that passes through.
	StyleWaveThrough = SpinnerStyle{"⠤", "⠶", "⠿", "⠛", "⠉", " ", " "}
	// StyleBouncingWave is similar to StyleWaveThrough but bounces back down.
	StyleBouncingWave = SpinnerStyle{"⠤", "⠤", "⠶", "⠿", "⠛", "⠉", "⠉", "⠛", "⠿", "⠶"}
	// StyleBouncingLine is a braille line bouncing up and down.
	StyleBouncingLine = SpinnerStyle{"⠤", "⠤", "⠒", "⠉", "⠉", "⠒"}
	// StyleSweepingDots is a sweeping dots animation along the perimeter.
	StyleSweepingDots = SpinnerStyle{"⣷", "⣯", "⣟", "⡿", "⢿", "⣻", "⣽", "⣾"}
	// StyleLine is a classic rotating ASCII line.
	StyleLine = SpinnerStyle{"-", "\\", "|", "/"}
	// StyleArc is a spinning arc segment.
	StyleArc = SpinnerStyle{"◜", "◠", "◝", "◞", "◡", "◟"}
	// StyleBarThrough is an ASCII bar passing through.
	StyleBarThrough = SpinnerStyle{"[    ]", "[    ]", "[=   ]", "[==  ]", "[=== ]", "[====]", "[ ===]", "[  ==]", "[   =]"}
	// StyleDotsDrop simulates dots dropping.
	StyleDotsDrop = SpinnerStyle{"⠁", "⠂", "⠄", "⡀", "⡈", "⡐", "⡠", "⣀", "⣁", "⣂", "⣄", "⣌", "⣔", "⣤", "⣥", "⣦", "⣮", "⣶", "⣷", "⣿", "⡿", "⠿", "⢟", "⠟", "⡛", "⠛", "⠫", "⢋", "⠋", "⠍", "⡉", "⠉", "⠑", "⠡", "⢁"}
	// StyleDotsUpload first fills upwards, then animates them moving up dot by dot.
	StyleDotsUpload = SpinnerStyle{" ", " ", " ", " ", " ", " ", " ", " ", "⣀", "⣤", "⣶", "⣿", "⣷", "⣶", "⣮", "⣦", "⣥", "⣤", "⣔", "⣌", "⣄", "⣂", "⣁", "⣀", "⡠", "⡐", "⡈", "⡀", "⠄", "⠂", "⠁"}
	// StyleDotsConstruct appears to build downwards. As each individual dot hits the surface, it bounces. After it is full, there is a brief pause.
	StyleDotsConstruct = SpinnerStyle{
		" ", " ", " ",
		"⠁", "⠂", "⠄", "⡀", "⠄", "⡀",
		"⡈", "⡐", "⡠", "⣀", "⡠", "⣀",
		"⣁", "⣂", "⣄", "⣂", "⣄",
		"⣌", "⣔", "⣤", "⣔", "⣤",
		"⣥", "⣦", "⣥", "⣦",
		"⣮", "⣶", "⣮", "⣶",
		"⣷", "⣶", "⣷",
		"⣿", "⣷", "⣿",
		"⣿", "⣿", "⣿", "⣿", "⣿",
		"⣶", "⣤", "⣀", " ", " ", " ",
	}

	// StyleSimpleDots is a simple progressing dots animation.
	StyleSimpleDots = SpinnerStyle{".  ", ".. ", "...", "   ", "   "}
	// StyleStar rotates a star-like pattern.
	StyleStar = SpinnerStyle{"✶", "✸", "✹", "✺", "✹", "✷"}
	// StyleFlip simulates a line flipping over.
	StyleFlip = SpinnerStyle{"_", "_", "_", "-", "`", "`", "'", "´", "-", "_", "_", "_"}
	// StyleNoise changes through visual noise blocks.
	StyleNoise = SpinnerStyle{"▓", "▒", "░"}
	// StyleCircleHalves rotates half circles.
	StyleCircleHalves = SpinnerStyle{"◐", "◓", "◑", "◒"}
	// StyleToggle alternates between two states.
	StyleToggle = SpinnerStyle{"□", "■"}
	// StyleBouncingBall is a ball bouncing horizontally.
	StyleBouncingBall = SpinnerStyle{"( ●    )", "(  ●   )", "(   ●  )", "(    ● )", "(     ●)", "(    ● )", "(   ●  )", "(  ●   )", "( ●    )", "(●     )"}
)

// SpinnerColor specifies the coloring mode for the spinner.
type SpinnerColor int

const (
	// ColorDefault uses the normal terminal text color.
	ColorDefault SpinnerColor = iota
	// ColorRGB makes the spinner cycle smoothly through rainbow colors per frame.
	ColorRGB
	// ColorRed applies a fixed red color to the spinner.
	ColorRed
	// ColorGreen applies a fixed green color to the spinner.
	ColorGreen
	// ColorYellow applies a fixed yellow color to the spinner.
	ColorYellow
	// ColorBlue applies a fixed blue color to the spinner.
	ColorBlue
	// ColorMagenta applies a fixed magenta color to the spinner.
	ColorMagenta
	// ColorCyan applies a fixed cyan color to the spinner.
	ColorCyan
)

// NewSpinner creates a new terminal spinner.
func NewSpinner(ctx context.Context, out io.Writer, interval time.Duration, pos SpinnerPosition, style SpinnerStyle, col SpinnerColor, dir SpinnerDirection) *Spinner {
	ctx, cancel := context.WithCancel(ctx)

	if len(style) == 0 {
		style = StyleSpinningDots
	}

	return &Spinner{
		out:      out,
		frames:   style,
		position: pos,
		color:    col,
		reverse:  dir == DirectionReverse,
		interval: interval,
		ctx:      ctx,
		cancel:   cancel,
		updateCh: make(chan string),
		styleCh:  make(chan SpinnerStyle),
		doneCh:   make(chan struct{}),
	}
}

// Start begins spinning in the background. It displays the initial message.
func (s *Spinner) Start(initialMessage string) {
	s.message = initialMessage
	go s.spin()
}

// Stop stops the spinner. It prints a final message without a spinner frame, if finalMessage is provided.
func (s *Spinner) Stop(finalMessage string) {
	s.cancel()
	<-s.doneCh
	s.Clear()
	if finalMessage != "" {
		fmt.Fprintf(s.out, "%s\n", finalMessage)
	}
}

// UpdateMessage changes the currently displayed message.
func (s *Spinner) UpdateMessage(msg string) {
	s.updateCh <- msg
}

// UpdateStyle changes the currently displayed spinner style animation.
func (s *Spinner) UpdateStyle(style SpinnerStyle) {
	s.styleCh <- style
}

// Clear removes the current spinner line from the output.
func (s *Spinner) Clear() {
	if s.lastPrintLen > 0 {
		fmt.Fprintf(s.out, "\r%-*s\r", s.lastPrintLen, "")
		s.lastPrintLen = 0
	}
}

// Interrupt allows printing a message that will remain on screen above the spinner.
func (s *Spinner) Interrupt(msg string) {
	s.updateCh <- "\x00" + msg // Special control sequence for interrupt
}

func (s *Spinner) printFrame() {
	if s.message == "" {
		s.Clear()
		return
	}

	frame := s.frames[s.frameIdx]
	if s.color == ColorRGB {
		// Cycle smoothly through the RGB rainbow.
		// Use the frame index to calculate a hue from 0 to 360.
		hue := float64((s.frameIdx * 15) % 360)
		r, g, b := hsvToRGB(hue, 1.0, 1.0)
		frame = fmt.Sprintf("\x1b[38;2;%d;%d;%dm%s\x1b[0m", r, g, b, frame)
	} else if s.color != ColorDefault {
		var c color.Color
		switch s.color {
		case ColorRed:
			c = color.C.Red()
		case ColorGreen:
			c = color.C.Green()
		case ColorYellow:
			c = color.C.Yellow()
		case ColorBlue:
			c = color.C.Blue()
		case ColorMagenta:
			c = color.C.Magenta()
		case ColorCyan:
			c = color.C.Cyan()
		}
		frame = c.Sprintf("%s", frame)
	}

	var printMsg string
	if s.position == PositionFront {
		printMsg = fmt.Sprintf("%s %s", frame, s.message)
	} else {
		printMsg = fmt.Sprintf("%s %s", s.message, frame)
	}
	fmt.Fprintf(s.out, "\r%-*s\r%s", s.lastPrintLen, "", printMsg)
	s.lastPrintLen = len(printMsg)

	if s.reverse {
		s.frameIdx--
		if s.frameIdx < 0 {
			s.frameIdx = len(s.frames) - 1
		}
	} else {
		s.frameIdx = (s.frameIdx + 1) % len(s.frames)
	}
}

func (s *Spinner) spin() {
	defer close(s.doneCh)
	ticker := time.NewTicker(s.interval)
	defer ticker.Stop()

	s.printFrame()

	for {
		select {
		case <-s.ctx.Done():
			return
		case msg := <-s.updateCh:
			if len(msg) > 0 && msg[0] == '\x00' {
				// Interrupt: Clear, print the interrupt message, then redraw the current frame.
				interruptMsg := msg[1:]
				s.Clear()
				fmt.Fprintln(s.out, interruptMsg)
				s.printFrame()
			} else {
				if s.message != msg {
					s.message = msg
					s.Clear()
					s.printFrame()
				}
			}
		case style := <-s.styleCh:
			if len(style) > 0 {
				s.frames = style
				if s.frameIdx >= len(s.frames) {
					s.frameIdx = 0
				}
				s.Clear()
				s.printFrame()
			}
		case <-ticker.C:
			s.printFrame()
		}
	}
}
