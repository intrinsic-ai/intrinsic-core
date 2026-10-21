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

// Package color contains helper functions to simplify color-printing to the terminal.
package color

import (
	"fmt"
	"io"
	"os"
)

const reset = "\x1b[0m"

type Color struct {
	fg string
	bg string
}

func newColor() Color { return Color{} }

// C initializes a builder-like object. Call color modification functions on it, and run [Printf] in
// the end. E.g. `color.C.Red().Printf("hello")` will print "hello" in red foreground.
var C = newColor()

// Black changes foreground color to black.
func (c Color) Black() Color {
	return Color{fg: "\x1b[30m", bg: c.bg}
}

// Red changes foreground color to red.
func (c Color) Red() Color {
	return Color{fg: "\x1b[31m", bg: c.bg}
}

// Green changes foreground color to green.
func (c Color) Green() Color {
	return Color{fg: "\x1b[32m", bg: c.bg}
}

// Yellow changes foreground color to yellow.
func (c Color) Yellow() Color {
	return Color{fg: "\x1b[33m", bg: c.bg}
}

// Blue changes foreground color to blue.
func (c Color) Blue() Color {
	return Color{fg: "\x1b[34m", bg: c.bg}
}

// Magenta changes foreground color to magenta.
func (c Color) Magenta() Color {
	return Color{fg: "\x1b[35m", bg: c.bg}
}

// Cyan changes foreground color to cyan.
func (c Color) Cyan() Color {
	return Color{fg: "\x1b[36m", bg: c.bg}
}

// White changes foreground color to white.
func (c Color) White() Color {
	return Color{fg: "\x1b[37m", bg: c.bg}
}

// Default changes foreground color to default.
func (c Color) Default() Color {
	return Color{fg: "\x1b[39m", bg: c.bg}
}

// LightGray changes foreground color to light gray.
func (c Color) LightGray() Color {
	return Color{fg: "\x1b[90m", bg: c.bg}
}

// LightRed changes foreground color to light red.
func (c Color) LightRed() Color {
	return Color{fg: "\x1b[91m", bg: c.bg}
}

// LightGreen changes foreground color to light green.
func (c Color) LightGreen() Color {
	return Color{fg: "\x1b[92m", bg: c.bg}
}

// LightYellow changes foreground color to light yellow.
func (c Color) LightYellow() Color {
	return Color{fg: "\x1b[93m", bg: c.bg}
}

// LightBlue changes foreground color to light blue.
func (c Color) LightBlue() Color {
	return Color{fg: "\x1b[94m", bg: c.bg}
}

// LightMagenta changes foreground color to light magenta.
func (c Color) LightMagenta() Color {
	return Color{fg: "\x1b[95m", bg: c.bg}
}

// LightCyan changes foreground color to light cyan.
func (c Color) LightCyan() Color {
	return Color{fg: "\x1b[96m", bg: c.bg}
}

// LightWhite changes foreground color to light white.
func (c Color) LightWhite() Color {
	return Color{fg: "\x1b[97m", bg: c.bg}
}

// BlackBackground changes background color to black.
func (c Color) BlackBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[40m"}
}

// RedBackground changes background color to red.
func (c Color) RedBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[41m"}
}

// GreenBackground changes background color to green.
func (c Color) GreenBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[42m"}
}

// YellowBackground changes background color to yellow.
func (c Color) YellowBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[43m"}
}

// BlueBackground changes background color to blue.
func (c Color) BlueBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[44m"}
}

// MagentaBackground changes background color to magenta.
func (c Color) MagentaBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[45m"}
}

// CyanBackground changes background color to cyan.
func (c Color) CyanBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[46m"}
}

// WhiteBackground changes background color to white.
func (c Color) WhiteBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[47m"}
}

// DefaultBackground changes background color to default.
func (c Color) DefaultBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[49m"}
}

// LightGrayBackground changes background color to light gray.
func (c Color) LightGrayBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[100m"}
}

// LightRedBackground changes background color to light red.
func (c Color) LightRedBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[101m"}
}

// LightGreenBackground changes background color to light green.
func (c Color) LightGreenBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[102m"}
}

// LightYellowBackground changes background color to light yellow.
func (c Color) LightYellowBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[103m"}
}

// LightBlueBackground changes background color to light blue.
func (c Color) LightBlueBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[104m"}
}

// LightMagentaBackground changes background color to light magenta.
func (c Color) LightMagentaBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[105m"}
}

// LightCyanBackground changes background color to light cyan.
func (c Color) LightCyanBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[106m"}
}

// LightWhiteBackground changes background color to light white.
func (c Color) LightWhiteBackground() Color {
	return Color{fg: c.fg, bg: "\x1b[107m"}
}

// Printf formats according to a format specifier and writes to standard output. The foreground
// and background colors are controlled by the [C] object. It returns the number of bytes written
// and any write error encountered.
func (c Color) Printf(format string, a ...any) (int, error) {
	return c.Fprintf(os.Stdout, format, a...)
}

// Fprintf formats according to a format specifier and writes to w. The foreground
// and background colors are controlled by the [C] object. It returns the number of bytes written
// and any write error encountered.
func (c Color) Fprintf(w io.Writer, format string, a ...any) (int, error) {
	return fmt.Fprintf(w, c.fg+c.bg+format+reset, a...)
}

// Sprintf formats according to a format specifier and returns the resulting string.
func (c Color) Sprintf(format string, a ...any) string {
	return fmt.Sprintf(c.fg+c.bg+format+reset, a...)
}
