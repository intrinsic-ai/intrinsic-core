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

// Package printer provides utilities for inctl printing.
package printer

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"reflect"
	"slices"
	"strings"
	"text/tabwriter"
	"unicode"

	"github.com/spf13/cobra"
)

const (
	// KeyOutput is a string used to refer the output flag.
	KeyOutput = "output"
	// JSONOutputFormat is a string indicating JSON output format.
	JSONOutputFormat = "json"
	// NDJSONOutputFormat is a string indicating NDJSON output format.
	NDJSONOutputFormat = "ndjson"
	// TextOutputFormat is a string indicating human-readable text output format.
	TextOutputFormat = ""
	// TabOutputFormat is a string indicating text output in tabulated format.
	TabOutputFormat = "tab"
)

// AllowedFormats is a list of possible output formats.
var AllowedFormats = []string{JSONOutputFormat, NDJSONOutputFormat}

// Message defines a struct for printing a single message in JSON format.
type Message struct {
	Msg string `json:"msg"`
}

func (m *Message) String() string {
	return m.Msg
}

// PrintMsg prints a simple message in a structurally safe way across all output formats.
func PrintMsg(prtr CommandPrinter, msg string) {
	msg = strings.TrimSuffix(msg, "\n")
	prtr.Println(&Message{Msg: msg})
}

// PrintMsgf formats and prints a message in a structurally safe way across all output formats.
func PrintMsgf(prtr CommandPrinter, format string, args ...any) {
	msg := fmt.Sprintf(format, args...)
	msg = strings.TrimSuffix(msg, "\n")
	prtr.Println(&Message{Msg: msg})
}

// Printer is the interface that wraps the basic Print methods.
// DEPRECATED: Use CommandPrinter instead.
type Printer interface {
	io.Writer
	Print(val any)
	PrintS(str string)
	PrintSf(format string, a ...any)
}

// JSONPrinter implements Printer.
// DEPRECATED: Use NewPrinterFromCommand to switch to new CommandPrinter interface.
type JSONPrinter struct {
	enc *json.Encoder
}

func (p *JSONPrinter) Write(c []byte) (n int, err error) {
	p.PrintS(string(c))
	return len(c), nil
}

// Print prints val in JSON format.
func (p *JSONPrinter) Print(val any) {
	p.enc.Encode(val)
}

// PrintS prints a string as a JSON object with a single "msg" field.
func (p *JSONPrinter) PrintS(str string) {
	p.Print(&Message{Msg: str})
}

// PrintSf prints the formatted string as a JSON object with a single "msg" field.
func (p *JSONPrinter) PrintSf(format string, a ...any) {
	p.PrintS(fmt.Sprintf(format, a...))
}

// TextPrinter implements Printer.
// DEPRECATED: Use NewPrinterFromCommand to switch to new CommandPrinter interface.
type TextPrinter struct {
	w io.Writer
}

func (p *TextPrinter) Write(c []byte) (n int, err error) {
	return p.w.Write(c)
}

// Print prints val in human-readable text format.
func (p *TextPrinter) Print(val any) {
	var s string
	if ta, ok := val.(fmt.Stringer); ok {
		s = ta.String()
	} else {
		s = fmt.Sprintf("%v", val)
	}
	fmt.Fprintln(p.w, s)
}

// PrintS prints a string as a JSON object with a single "msg" field.
func (p *TextPrinter) PrintS(str string) {
	p.Print(str)
}

// PrintSf prints the formatted string as a JSON object with a single "msg" field.
func (p *TextPrinter) PrintSf(format string, a ...any) {
	p.PrintS(fmt.Sprintf(format, a...))
}

// NewPrinterWithWriter returns a new Printer which writes to the given writer
// using the given output format.
// DEPRECATED: Use NewPrinterFromCommand to switch to new CommandPrinter interface.
func NewPrinterWithWriter(outputFormat string, w io.Writer) (Printer, error) {
	ot := OutputTypeText
	switch outputFormat {
	case JSONOutputFormat:
		ot = OutputTypeJSON
	case TabOutputFormat:
		ot = OutputTypeTAB
	case TextOutputFormat:
		// this is fallback to support old style printer behavior.
		return &TextPrinter{w: w}, nil
	}

	cp, err := newPrinterFromType(ot, w, os.Stderr)
	if err != nil {
		return nil, fmt.Errorf("cannot create printer: %w", err)
	}

	return &legacyPrinterAdapter{cp: cp}, nil
}

// NewPrinter returns a new Printer which writes to os.Stdout using the given
// output format.
// DEPRECATED: Use NewPrinterFromCommand to switch to new CommandPrinter interface.
func NewPrinter(outputFormat string) (Printer, error) {
	return NewPrinterWithWriter(outputFormat, os.Stdout)
}

// AsPrinter tries to convert supplied io.Writer to Printer.
// If in parameter is of type Printer, it returns it without modification
//
// If in parameter is not of type Printer, returns new Printer with in as its
// internal writer if orElseType is specified or indicates failure if new
// Printer cannot be constructed.
//
// If in is not Printer and orElseType is not specified, method returns
// nil Printer indicating conversion didn't happen
// DEPRECATED: Use NewPrinterFromCommand to switch to new CommandPrinter interface.
func AsPrinter(in io.Writer, orElseType ...string) (Printer, bool) {
	if prt, ok := in.(Printer); ok {
		return prt, ok
	} else if len(orElseType) > 0 {
		prt, err := NewPrinterWithWriter(orElseType[0], in)
		return prt, err == nil
	}
	return nil, false
}

// Here starts next generation of printer output for commands

type legacyPrinterAdapter struct {
	cp CommandPrinter
}

func (l *legacyPrinterAdapter) Write(p []byte) (n int, err error) {
	l.cp.Print(fmt.Sprintf("%v", p))
	return len(p), nil
}

func (l *legacyPrinterAdapter) Print(val any) {
	l.cp.Print(val)
}

func (l *legacyPrinterAdapter) PrintS(str string) {
	l.cp.Print(str)
}

func (l *legacyPrinterAdapter) PrintSf(format string, a ...any) {
	l.cp.Printf(format, a...)
}

// OutputType represents information about desired command output format.
type OutputType int

const (
	// OutputTypeText indicates command output should be plain text
	OutputTypeText OutputType = iota
	// OutputTypeJSON indicates command output should be in form where each line is valid JSON
	OutputTypeJSON
	// OutputTypeNDJSON indicates command output should be in form where each line is valid JSON
	OutputTypeNDJSON
	// OutputTypeTAB indicates command output should be tabulated
	OutputTypeTAB
)

const (
	// DefaultTabulatedTag designates a reflect.Field.Tag value to check
	// in order to identify fields which should be printed in a row.
	// By default, we fully piggyback on json tags
	DefaultTabulatedTag = "json"
)

// CanDecorate returns true if output type is suitable for using decorations
// such as headers and footers for output, as in terminal console. Allows
// caller to modify output behavior based on expected result.
func (x OutputType) CanDecorate() bool {
	return x != OutputTypeJSON
}

// Wrap allows to wrap simple string in appropriate way for given output type.
func (x OutputType) Wrap(s string) any {
	if x == OutputTypeJSON {
		return Message{Msg: s}
	}
	return s
}

// Tabulator defines conversion from a given value into columns of the table row.
type Tabulator interface {
	// Tabulated returns array of objects to be written by tabulated output on a single line
	Tabulated(names []string) []string
}

// View defines all the interfaces needed to support any available printer in this package.
type View interface {
	fmt.Stringer
	json.Marshaler
	Tabulator
}

// NextView allows supplying given View with new value, in order to reuse
// previous internal state of the view. Recommended for TAB printer if you
// plan to wrap your values with SimpleView.
func NextView(val any, view View) View {
	dw, ok := view.(*defView)
	if !ok {
		return SimpleView(val)
	}
	dw.value = val
	return dw
}

// SimpleView wraps provided val in default view behavior, useful if you
// don't want to provide your own implementation of View for your
// data class.
func SimpleView(val any) View {
	return &defView{value: val}
}

type defView struct {
	value     any
	reflector *reflector
}

func (d *defView) String() string {
	return fmt.Sprintf("%v", d.value)
}

func (d *defView) MarshalJSON() ([]byte, error) {
	return json.Marshal(d.value)
}

func (d *defView) Tabulated(columns []string) []string {
	if tbltr, ok := d.value.(Tabulator); ok {
		// just in case before we start heavy lifting.
		return tbltr.Tabulated(columns)
	}
	val := reflect.ValueOf(d.value)
	switch val.Kind() {
	case reflect.Array:
		result := make([]string, val.Len())
		for i := 0; i < val.Len(); i++ {
			result[i] = fmt.Sprint(val.Index(i).Interface())
		}
		return result
	case reflect.Map:
		result := make([]string, len(columns))
		for _, key := range val.MapKeys() {
			if idx := slices.Index(columns, key.String()); idx >= 0 {
				result[idx] = fmt.Sprintf("%v: %v", key, val.MapIndex(key))
			}
		}
		return result
	case reflect.Interface:
		return d.reflectValue(columns, val)
	case reflect.Pointer:
		return d.reflectValue(columns, val)
	case reflect.Struct:
		return d.reflectValue(columns, val)
	default:
		stringVal := fmt.Sprint(d.value)
		return strings.Split(stringVal, "\t")
	}
}

func (d *defView) reflectValue(columns []string, val reflect.Value) []string {
	if d.reflector == nil || val.Type().AssignableTo(d.reflector.valType) {
		// the idea here is that it is rare that you would print multiple rows
		// from different types, but rather would print many rows of same type,
		// thus we optimize for that while we keep only one instance of reflector for view.
		var err error
		d.reflector, err = newReflectorFromTags(d.value, DefaultTabulatedTag)
		if err != nil {
			fmt.Fprintln(os.Stderr, fmt.Sprintf("Error obtaining reflector for value: %v", err))
			return nil
		}
	}
	return d.reflector.columnsFromFields(d.value, columns)
}

// CommandPrinter interface matches cobra.Command output helpers to simplify
// facilitating different outputs.
type CommandPrinter interface {
	// Print is a convenience method to Print to the defined output, fallback to Stderr if not set.
	Print(i ...any)
	// Println is a convenience method to Println to the defined output, fallback to Stderr if not set.
	Println(i ...any)
	// Printf is a convenience method to Printf to the defined output, fallback to Stderr if not set.
	Printf(format string, i ...any)
	// PrintErr is a convenience method to Print to the defined Err output, fallback to Stderr if not set.
	PrintErr(i ...any)
	// PrintErrln is a convenience method to Println to the defined Err output, fallback to Stderr if not set.
	PrintErrln(i ...any)
	// PrintErrf is a convenience method to Printf to the defined Err output, fallback to Stderr if not set.
	PrintErrf(format string, i ...any)
}

type commandPrinterKey string

var commandPrinterCtxKey commandPrinterKey = "printer.CommandPrinter.CtxValue"

// Formatting can be controlled with these flags. These constants shadow
// tabwriter.Writer flags in order to hide implementation.
const (
	// FilterHTML ignores HTML tags and treat entities (starting with '&'
	// and ending in ';') as single characters (width = 1).
	FilterHTML = tabwriter.FilterHTML

	// StripEscape strips Escape characters bracketing escaped text segments
	// instead of passing them through unchanged with the text.
	StripEscape = tabwriter.StripEscape

	// AlignRight forces right-alignment of cell content.
	// Default is left-alignment.
	AlignRight = tabwriter.AlignRight

	// DiscardEmptyColumns handles empty columns as if they were not present in
	// the input in the first place.
	DiscardEmptyColumns = tabwriter.DiscardEmptyColumns

	// TabIndent always uses tabs for indentation columns (i.e., padding of
	// leading empty cells on the left) independent of TableConfig.PadChar.
	TabIndent = tabwriter.TabIndent

	// DrawCells prints a vertical bar ('|') between columns (after formatting).
	// Discarded columns appear as zero-width columns ("||").
	DrawCells = tabwriter.Debug
)

// TableConfig represents simple for cells of the table for TAB writer.
// For details see tabwriter.Writer#Init()
type TableConfig struct {
	// minimal cell width including any padding
	MinWidth int
	// width of tab characters (equivalent number of spaces)
	TabWidth int
	// padding added to a cell before computing its width
	Padding int
	// ASCII char used for padding
	PadChar byte
	// formatting control
	Flags uint
}

// DefaultTableConfig represents default, compact, cell settings.
var DefaultTableConfig = TableConfig{
	MinWidth: 8,
	TabWidth: 1,
	Padding:  1,
	PadChar:  ' ',
	Flags:    TabIndent | DiscardEmptyColumns,
}

// Option allows to configure a printer of given type.
// If option does not support given OutputType, it should silently
// ignore it and move on.
// No error shall be reported for unsupported OutputType.
type Option func(ot OutputType, printer any) error

// ColumnNameReplacer allows to supply alternative names for columns
type ColumnNameReplacer func(column string) string

// ColumnOrdering allows to rewrite order of columns as user desires.
// Resulting array have to have names which are valid for given printer,
// but can be smaller if not all columns are requires to be shown.
// Implementation may manipulate input array directly
type ColumnOrdering func(columns []string) []string

// SortByName is ColumnOrdering which sorts columns by their name.
// Use carefully as your output may be weird.
var SortByName ColumnOrdering = func(columns []string) []string {
	slices.SortFunc(columns, strings.Compare)
	return columns
}

// WithDefaultsFromValue configures TAB printer with well-meaning defaults which
// should satisfy most common needs for the tabular output of this value.
// Value must be either interface or struct, error is returned otherwise.
// This value always performs reflection on value to obtain names of the
// columns which are then sorted lexicographically for output.
// This Option generally does not need to be followed by WithHeaderOverride option.
func WithDefaultsFromValue(value any, order ColumnOrdering) Option {
	return WithTableFromValue(DefaultTableConfig, value, order)
}

// WithTableConfig option configures TAB printer with provided TableConfig and
// list of columns it should print into output. The list of columns is later
// passed into Tabulator.Tabulated function to guide tabulation of data.
// If values passed to printer don't implement Tabulator interface, columns
// should match names of exported fields if SimpleView or NextView is used
// for wrapping values.
func WithTableConfig(tc TableConfig, columns ...string) Option {
	return func(ot OutputType, printer any) error {
		if ot != OutputTypeTAB {
			return nil
		}
		tp := printer.(*tabPrinter)
		tp.prettyHeader = Capitalize
		tp.columns = columns
		tp.outW = tabwriter.NewWriter(tp.rawOutW, tc.MinWidth, tc.TabWidth, tc.Padding, tc.PadChar, tc.Flags)
		return nil
	}
}

// WithHeaderOverride option sets TAB printer header, overriding any automatic
// header. To reset automatic header, provide empty list of headerCols.
// This option should be always last in the list of options to ensure
// none previous option overrides printer header.
func WithHeaderOverride(pretty ColumnNameReplacer) Option {
	return func(ot OutputType, printer any) error {
		if ot != OutputTypeTAB {
			return nil
		}
		tp := printer.(*tabPrinter)
		tp.prettyHeader = pretty
		return nil
	}
}

// WithTableFromValue option setups TAB printer with given TableConfig and
// derives columns from given value based on type reflection.
func WithTableFromValue(tc TableConfig, value any, order ColumnOrdering) Option {
	return func(ot OutputType, printer any) error {
		if ot != OutputTypeTAB {
			return nil
		}

		columns, err := tableColumnsFromValue(DefaultTabulatedTag, value)
		if err != nil {
			return fmt.Errorf("cannot configure columns: %w", err)
		}
		if order != nil {
			columns = order(columns)
		}

		optionFx := WithTableConfig(tc, columns...)
		return optionFx(ot, printer)
	}
}

// WithPrettyJSON is printer option which prints JSON with indentations.
func WithPrettyJSON() Option {
	return func(ot OutputType, printer any) error {
		if ot == OutputTypeJSON {
			jsonP, ok := printer.(*jsonPrinter)
			if !ok {
				return errors.New("JSON format indicated but printer not *jsonPrinter")
			}
			jsonP.enc.SetIndent("", "\t")
		}
		return nil
	}
}

func tableColumnsFromValue(tag string, value any) ([]string, error) {
	reflctr, err := newReflectorFromTags(value, tag)
	if err != nil {
		return nil, fmt.Errorf("cannot configure tabulation: %w", err)
	}

	columns := make([]string, len(reflctr.fields))
	idx := 0
	for key := range reflctr.fields {
		columns[idx] = key
		idx++
	}

	return columns, nil
}

// NewPrinterFromCommand returns a new CommandPrinter most suitable
// for given command. The commands KeyOutput flag is consulted in order to determine
// the best printer.
func NewPrinterFromCommand(cmd *cobra.Command, opts ...Option) (CommandPrinter, error) {
	return NewPrinterOfType(GetFlagOutputType(cmd), cmd, opts...)
}

// GetFlagOutputType converts value of "output" tag into OutputType.
func GetFlagOutputType(cmd *cobra.Command) OutputType {
	if cmd == nil {
		return OutputTypeText
	}
	flag := cmd.Flag(KeyOutput)
	if flag == nil {
		return OutputTypeText
	}
	switch flag.Value.String() {
	case JSONOutputFormat:
		return OutputTypeJSON
	case NDJSONOutputFormat:
		return OutputTypeNDJSON
	case TabOutputFormat:
		return OutputTypeTAB
	default:
		return OutputTypeText
	}
}

// NewPrinterOfType returns a new CommandPrinter of type OutputType
// ignoring KeyOutput parameter of given command.
func NewPrinterOfType(ot OutputType, cmd *cobra.Command, opts ...Option) (CommandPrinter, error) {
	return newPrinterFromType(ot, cmd.OutOrStdout(), cmd.OutOrStderr(), opts...)
}

// GetDefaultPrinter returns default output printer for command. Use as backup
// option if getting printer from New* methods fails.
func GetDefaultPrinter(cmd *cobra.Command) CommandPrinter {
	return cmd
}

func newPrinterFromType(ot OutputType, outW, errW io.Writer, opts ...Option) (cp CommandPrinter, err error) {
	switch ot {
	case OutputTypeTAB:
		cp = &tabPrinter{
			errOutPrinter: errOutPrinter{
				errW: errW,
			},
			rawOutW: outW,
			outW: tabwriter.NewWriter(outW, DefaultTableConfig.MinWidth, DefaultTableConfig.TabWidth,
				DefaultTableConfig.Padding, DefaultTableConfig.PadChar, DefaultTableConfig.Flags),
		}
	case OutputTypeJSON, OutputTypeNDJSON:
		cp = &jsonPrinter{
			errOutPrinter: errOutPrinter{
				errW: errW,
			},
			w:   outW,
			enc: json.NewEncoder(outW),
		}
	case OutputTypeText:
		cp = &textPrinter{
			errOutPrinter: errOutPrinter{
				errW: errW,
			},
			w: outW,
		}
	default:
		return nil, fmt.Errorf("invalid output type, only text, TAB and JSON are supported")
	}

	for _, opt := range opts {
		err = opt(ot, cp)
		if err != nil {
			return nil, fmt.Errorf("cannot configure printer: %w", err)
		}
	}

	return cp, nil
}

// AttachPrinterToContext attaches given printer to the given command.
func AttachPrinterToContext(ctx context.Context, printer CommandPrinter) context.Context {
	return context.WithValue(ctx, commandPrinterCtxKey, printer)
}

// GetPrinterFromContext retrieves CommandPrinter implementation from given context
// if present. Returns false as second result if printer cannot be found on context
// or is not CommandPrinter.
func GetPrinterFromContext(ctx context.Context) (CommandPrinter, bool) {
	result := ctx.Value(commandPrinterCtxKey)
	commandPrinter, ok := result.(CommandPrinter)
	return commandPrinter, ok
}

// GetOutputType determine output type for given CommandPrinter. This method is
// useful if caller wants to determine current OutputType for any reason.
func GetOutputType(cp CommandPrinter) OutputType {
	if _, ok := cp.(*tabPrinter); ok {
		return OutputTypeTAB
	}
	if _, ok := cp.(*jsonPrinter); ok {
		return OutputTypeJSON
	}
	return OutputTypeText
}

// Flush flushes printer's underlying buffer. Only OutputTypeTAB requires
// this operation at the moment. For all other printers, this is no-op.
func Flush(cp CommandPrinter) error {
	if tabs, ok := cp.(*tabPrinter); ok {
		return tabs.outW.Flush()
	}
	return nil
}

type reflector struct {
	valType reflect.Type
	fields  map[string]reflect.StructField
}

func (r *reflector) columnsFromFields(val any, columns []string) []string {
	result := make([]string, len(columns))
	rVal := reflect.ValueOf(val)
	rVal = reflect.Indirect(rVal)
	for idx, column := range columns {
		result[idx] = fmt.Sprint(rVal.FieldByName(r.fields[column].Name).Interface())
	}
	return result
}

// this is super costly, so we want to do it as little as possible.
func newReflectorFromTags(value any, tagKey string) (*reflector, error) {
	rType := reflect.TypeOf(value)
	if rType.Kind() == reflect.Interface || rType.Kind() == reflect.Pointer {
		rType = reflect.Indirect(reflect.ValueOf(value)).Type()
	}
	if rType.Kind() != reflect.Struct {
		return nil, fmt.Errorf("invalid type %s, must be struct or pointer to struct", rType)
	}
	fields := reflect.VisibleFields(rType)
	columnMap := make(map[string]reflect.StructField)
	for _, field := range fields {
		tagValue, exists := field.Tag.Lookup(tagKey)
		// we follow JSON Marshal logic here: https://pkg.go.dev/encoding/json#Marshal
		if !exists || tagValue == "-" {
			continue
		}
		if idx := strings.Index(tagValue, ","); idx >= 0 {
			tagValue = tagValue[:idx] // trim excess (this is mostly if we read from json tags)
		}
		if tagValue == "" {
			tagValue = field.Name
		}
		columnMap[tagValue] = field
	}

	return &reflector{
		valType: rType,
		fields:  columnMap,
	}, nil
}

type errOutPrinter struct {
	errW io.Writer
}

func (e *errOutPrinter) PrintErr(i ...any) {
	fmt.Fprint(e.errW, i...)
}

func (e *errOutPrinter) PrintErrln(i ...any) {
	fmt.Fprintln(e.errW, i...)
}

func (e *errOutPrinter) PrintErrf(format string, i ...any) {
	fmt.Fprintf(e.errW, format, i...)
}

// jsonPrinter is special implementation to support simple output of elements
// as JSON. Implementation has two caveats:
// 1. It expects every input interface{} to be marshalled into JSON directly.
// 2. All error outputs are printed to errOut of the cmd in plain text, no JSON encoding is happening.
type jsonPrinter struct {
	errOutPrinter
	w   io.Writer
	enc *json.Encoder
}

func (j *jsonPrinter) Print(i ...any) {
	var err error
	for _, ii := range i {
		err = j.enc.Encode(ii)
		if err != nil {
			j.PrintErrln(err)
		}
	}
}

func (j *jsonPrinter) Println(i ...any) {
	var err error
	for _, ii := range i {
		err = j.enc.Encode(ii)
		if err != nil {
			j.PrintErrln(err)
		}
	}
}

// Printf ignores the first parameter to ensure each line is valid JSON text.
func (j *jsonPrinter) Printf(_ string, i ...any) {
	j.Println(i...)
}

type textPrinter struct {
	errOutPrinter
	w io.Writer
}

func (t *textPrinter) Print(i ...any) {
	fmt.Fprint(t.w, i...)
}

func (t *textPrinter) Println(i ...any) {
	fmt.Fprintln(t.w, i...)
}

func (t *textPrinter) Printf(format string, i ...any) {
	fmt.Fprintf(t.w, format, i...)
}

// Prints values in rows of neatly formatted table. All Print methods
// treat each input value as single row in a table.
// See View, SimpleView, WithTableFromValue, WithDefaultsFromValue for more details.
type tabPrinter struct {
	errOutPrinter
	columns      []string
	rawOutW      io.Writer
	outW         *tabwriter.Writer
	headerOut    bool
	prettyHeader ColumnNameReplacer
}

func (t *tabPrinter) Print(i ...any) {
	if !t.headerOut {
		// Lazy header printing, maybe we can do this better.
		t.printPrettyHeader()
		t.headerOut = true
	}
	for _, val := range i {
		line := ""
		if tabulated, ok := val.(Tabulator); ok {
			line = strings.Join(tabulated.Tabulated(t.columns), "\t")
		} else {
			line = fmt.Sprint(val)
		}
		fmt.Fprintln(t.outW, line)
	}
}

func (t *tabPrinter) Println(i ...any) {
	t.Print(i...)
}

func (t *tabPrinter) Printf(format string, i ...any) {
	t.Print(fmt.Sprintf(format, i...))
}

func (t *tabPrinter) printPrettyHeader() {
	if t.prettyHeader == nil {
		t.prettyHeader = Capitalize
	}
	cols := make([]string, len(t.columns))
	for idx, column := range t.columns {
		cols[idx] = t.prettyHeader(column)
	}

	fmt.Fprintln(t.outW, strings.Join(cols, "\t"))
}

// Capitalize is simple helper method to Capitalize first letter of input string.
func Capitalize(name string) string {

	// this is intentionally implemented this way, so I don't have to wrestle copybara for now.
	// there is cleaner implementation using golang.org/x/text/cases
	// TODO (@rkomara): Use golang.org/x/text/cases once migration is done.

	if name == "" {
		return name
	}

	if len(name) == 1 {
		return strings.ToUpper(name)
	}

	return string(unicode.ToUpper(rune(name[0]))) + name[1:]
}

// PrettyHeaderColumn translates JSON snake cases into spaces.
func PrettyHeaderColumn(name string) string {
	if name == "" {
		return ""
	}
	name = strings.ReplaceAll(name, "_", " ")
	name = strings.ReplaceAll(name, "-", " ")
	return Capitalize(name)
}

// Ellipses shorten text and adds ellipses into the end if needed.
func Ellipses(s string, max int) string {
	if max < 0 {
		return s
	}
	sl := len(s)
	if max < 4 && sl > 3 {
		return "..."
	}
	if sl <= max {
		return s
	}
	return s[:(max-3)] + "..."
}
