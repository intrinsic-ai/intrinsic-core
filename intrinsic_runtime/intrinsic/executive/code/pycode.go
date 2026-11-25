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

// Package pycode provides the functionality for generating the Python code
// which is executed by the code execution service on the Jupyter server and for
// parsing back the results of the code executions.
package pycode

import (
	"bytes"
	"embed"
	"fmt"
	"strconv"
	"strings"
	"text/template"

	log "github.com/golang/glog"
	"google.golang.org/protobuf/proto"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
	anypb "google.golang.org/protobuf/types/known/anypb"
)

const (
	// ResetSessionCommand is the command sent to Jupyter to perform a lightweight
	// session reset between code execution requests.
	ResetSessionCommand = "%reset -f"

	executedCodeTemplateName      = "executed_code.py.template"
	displayedCodeTemplateName     = "displayed_code.py.template"
	seedCodeTemplateName          = "seed_code.py.template"
	defaultComputeBodyPlaceholder = "{compute_body}"
)

var (
	//go:embed templates/*
	embeddedTemplates embed.FS
	templateSet       *template.Template
)

func init() {
	var err error
	templateSet, err = template.New("").ParseFS(embeddedTemplates, "templates/*.template")
	if err != nil {
		log.Exit(err)
	}
}

// goByteToPythonBytesLiteral maps byte values to a corresponding string that
// can be included inside a Python bytes literal of the form b'...'. This table
// was generated using the following Python code (plus manual edits for ' and `):
//
//	for i in range(0,256): print(f"`{repr(bytes([i]))[2:-1]}`, // {i}, 0x{i:02x}")
var goByteToPythonBytesLiteral = []string{
	`\x00`, // 0, 0x00
	`\x01`, // 1, 0x01
	`\x02`, // 2, 0x02
	`\x03`, // 3, 0x03
	`\x04`, // 4, 0x04
	`\x05`, // 5, 0x05
	`\x06`, // 6, 0x06
	`\x07`, // 7, 0x07
	`\x08`, // 8, 0x08
	`\t`,   // 9, 0x09
	`\n`,   // 10, 0x0a
	`\x0b`, // 11, 0x0b
	`\x0c`, // 12, 0x0c
	`\r`,   // 13, 0x0d
	`\x0e`, // 14, 0x0e
	`\x0f`, // 15, 0x0f
	`\x10`, // 16, 0x10
	`\x11`, // 17, 0x11
	`\x12`, // 18, 0x12
	`\x13`, // 19, 0x13
	`\x14`, // 20, 0x14
	`\x15`, // 21, 0x15
	`\x16`, // 22, 0x16
	`\x17`, // 23, 0x17
	`\x18`, // 24, 0x18
	`\x19`, // 25, 0x19
	`\x1a`, // 26, 0x1a
	`\x1b`, // 27, 0x1b
	`\x1c`, // 28, 0x1c
	`\x1d`, // 29, 0x1d
	`\x1e`, // 30, 0x1e
	`\x1f`, // 31, 0x1f
	` `,    // 32, 0x20
	`!`,    // 33, 0x21
	`"`,    // 34, 0x22
	`#`,    // 35, 0x23
	`$`,    // 36, 0x24
	`%`,    // 37, 0x25
	`&`,    // 38, 0x26
	`\'`,   // 39, 0x27
	`(`,    // 40, 0x28
	`)`,    // 41, 0x29
	`*`,    // 42, 0x2a
	`+`,    // 43, 0x2b
	`,`,    // 44, 0x2c
	`-`,    // 45, 0x2d
	`.`,    // 46, 0x2e
	`/`,    // 47, 0x2f
	`0`,    // 48, 0x30
	`1`,    // 49, 0x31
	`2`,    // 50, 0x32
	`3`,    // 51, 0x33
	`4`,    // 52, 0x34
	`5`,    // 53, 0x35
	`6`,    // 54, 0x36
	`7`,    // 55, 0x37
	`8`,    // 56, 0x38
	`9`,    // 57, 0x39
	`:`,    // 58, 0x3a
	`;`,    // 59, 0x3b
	`<`,    // 60, 0x3c
	`=`,    // 61, 0x3d
	`>`,    // 62, 0x3e
	`?`,    // 63, 0x3f
	`@`,    // 64, 0x40
	`A`,    // 65, 0x41
	`B`,    // 66, 0x42
	`C`,    // 67, 0x43
	`D`,    // 68, 0x44
	`E`,    // 69, 0x45
	`F`,    // 70, 0x46
	`G`,    // 71, 0x47
	`H`,    // 72, 0x48
	`I`,    // 73, 0x49
	`J`,    // 74, 0x4a
	`K`,    // 75, 0x4b
	`L`,    // 76, 0x4c
	`M`,    // 77, 0x4d
	`N`,    // 78, 0x4e
	`O`,    // 79, 0x4f
	`P`,    // 80, 0x50
	`Q`,    // 81, 0x51
	`R`,    // 82, 0x52
	`S`,    // 83, 0x53
	`T`,    // 84, 0x54
	`U`,    // 85, 0x55
	`V`,    // 86, 0x56
	`W`,    // 87, 0x57
	`X`,    // 88, 0x58
	`Y`,    // 89, 0x59
	`Z`,    // 90, 0x5a
	`[`,    // 91, 0x5b
	`\\`,   // 92, 0x5c
	`]`,    // 93, 0x5d
	`^`,    // 94, 0x5e
	`_`,    // 95, 0x5f
	"`",    // 96, 0x60
	`a`,    // 97, 0x61
	`b`,    // 98, 0x62
	`c`,    // 99, 0x63
	`d`,    // 100, 0x64
	`e`,    // 101, 0x65
	`f`,    // 102, 0x66
	`g`,    // 103, 0x67
	`h`,    // 104, 0x68
	`i`,    // 105, 0x69
	`j`,    // 106, 0x6a
	`k`,    // 107, 0x6b
	`l`,    // 108, 0x6c
	`m`,    // 109, 0x6d
	`n`,    // 110, 0x6e
	`o`,    // 111, 0x6f
	`p`,    // 112, 0x70
	`q`,    // 113, 0x71
	`r`,    // 114, 0x72
	`s`,    // 115, 0x73
	`t`,    // 116, 0x74
	`u`,    // 117, 0x75
	`v`,    // 118, 0x76
	`w`,    // 119, 0x77
	`x`,    // 120, 0x78
	`y`,    // 121, 0x79
	`z`,    // 122, 0x7a
	`{`,    // 123, 0x7b
	`|`,    // 124, 0x7c
	`}`,    // 125, 0x7d
	`~`,    // 126, 0x7e
	`\x7f`, // 127, 0x7f
	`\x80`, // 128, 0x80
	`\x81`, // 129, 0x81
	`\x82`, // 130, 0x82
	`\x83`, // 131, 0x83
	`\x84`, // 132, 0x84
	`\x85`, // 133, 0x85
	`\x86`, // 134, 0x86
	`\x87`, // 135, 0x87
	`\x88`, // 136, 0x88
	`\x89`, // 137, 0x89
	`\x8a`, // 138, 0x8a
	`\x8b`, // 139, 0x8b
	`\x8c`, // 140, 0x8c
	`\x8d`, // 141, 0x8d
	`\x8e`, // 142, 0x8e
	`\x8f`, // 143, 0x8f
	`\x90`, // 144, 0x90
	`\x91`, // 145, 0x91
	`\x92`, // 146, 0x92
	`\x93`, // 147, 0x93
	`\x94`, // 148, 0x94
	`\x95`, // 149, 0x95
	`\x96`, // 150, 0x96
	`\x97`, // 151, 0x97
	`\x98`, // 152, 0x98
	`\x99`, // 153, 0x99
	`\x9a`, // 154, 0x9a
	`\x9b`, // 155, 0x9b
	`\x9c`, // 156, 0x9c
	`\x9d`, // 157, 0x9d
	`\x9e`, // 158, 0x9e
	`\x9f`, // 159, 0x9f
	`\xa0`, // 160, 0xa0
	`\xa1`, // 161, 0xa1
	`\xa2`, // 162, 0xa2
	`\xa3`, // 163, 0xa3
	`\xa4`, // 164, 0xa4
	`\xa5`, // 165, 0xa5
	`\xa6`, // 166, 0xa6
	`\xa7`, // 167, 0xa7
	`\xa8`, // 168, 0xa8
	`\xa9`, // 169, 0xa9
	`\xaa`, // 170, 0xaa
	`\xab`, // 171, 0xab
	`\xac`, // 172, 0xac
	`\xad`, // 173, 0xad
	`\xae`, // 174, 0xae
	`\xaf`, // 175, 0xaf
	`\xb0`, // 176, 0xb0
	`\xb1`, // 177, 0xb1
	`\xb2`, // 178, 0xb2
	`\xb3`, // 179, 0xb3
	`\xb4`, // 180, 0xb4
	`\xb5`, // 181, 0xb5
	`\xb6`, // 182, 0xb6
	`\xb7`, // 183, 0xb7
	`\xb8`, // 184, 0xb8
	`\xb9`, // 185, 0xb9
	`\xba`, // 186, 0xba
	`\xbb`, // 187, 0xbb
	`\xbc`, // 188, 0xbc
	`\xbd`, // 189, 0xbd
	`\xbe`, // 190, 0xbe
	`\xbf`, // 191, 0xbf
	`\xc0`, // 192, 0xc0
	`\xc1`, // 193, 0xc1
	`\xc2`, // 194, 0xc2
	`\xc3`, // 195, 0xc3
	`\xc4`, // 196, 0xc4
	`\xc5`, // 197, 0xc5
	`\xc6`, // 198, 0xc6
	`\xc7`, // 199, 0xc7
	`\xc8`, // 200, 0xc8
	`\xc9`, // 201, 0xc9
	`\xca`, // 202, 0xca
	`\xcb`, // 203, 0xcb
	`\xcc`, // 204, 0xcc
	`\xcd`, // 205, 0xcd
	`\xce`, // 206, 0xce
	`\xcf`, // 207, 0xcf
	`\xd0`, // 208, 0xd0
	`\xd1`, // 209, 0xd1
	`\xd2`, // 210, 0xd2
	`\xd3`, // 211, 0xd3
	`\xd4`, // 212, 0xd4
	`\xd5`, // 213, 0xd5
	`\xd6`, // 214, 0xd6
	`\xd7`, // 215, 0xd7
	`\xd8`, // 216, 0xd8
	`\xd9`, // 217, 0xd9
	`\xda`, // 218, 0xda
	`\xdb`, // 219, 0xdb
	`\xdc`, // 220, 0xdc
	`\xdd`, // 221, 0xdd
	`\xde`, // 222, 0xde
	`\xdf`, // 223, 0xdf
	`\xe0`, // 224, 0xe0
	`\xe1`, // 225, 0xe1
	`\xe2`, // 226, 0xe2
	`\xe3`, // 227, 0xe3
	`\xe4`, // 228, 0xe4
	`\xe5`, // 229, 0xe5
	`\xe6`, // 230, 0xe6
	`\xe7`, // 231, 0xe7
	`\xe8`, // 232, 0xe8
	`\xe9`, // 233, 0xe9
	`\xea`, // 234, 0xea
	`\xeb`, // 235, 0xeb
	`\xec`, // 236, 0xec
	`\xed`, // 237, 0xed
	`\xee`, // 238, 0xee
	`\xef`, // 239, 0xef
	`\xf0`, // 240, 0xf0
	`\xf1`, // 241, 0xf1
	`\xf2`, // 242, 0xf2
	`\xf3`, // 243, 0xf3
	`\xf4`, // 244, 0xf4
	`\xf5`, // 245, 0xf5
	`\xf6`, // 246, 0xf6
	`\xf7`, // 247, 0xf7
	`\xf8`, // 248, 0xf8
	`\xf9`, // 249, 0xf9
	`\xfa`, // 250, 0xfa
	`\xfb`, // 251, 0xfb
	`\xfc`, // 252, 0xfc
	`\xfd`, // 253, 0xfd
	`\xfe`, // 254, 0xfe
	`\xff`, // 255, 0xff
}

// GoBytesToPythonBytesLiteral converts the given sequence of bytes to a Python
// bytes literal using shortcut notation as far as possible. For example, the
// byte sequence [0x9e, 0x66, 0x6f, 0x6f, 0x7b, 0x7d, 0x0f] can always by
// written as `b'\x9e\x66\x6f\x6f\x7b\x7d\x0f'` in Python but this function will
// return `b'\x9efoo{}\x0f' which is both shorter and more human readable (if
// the content represents or contains text).
func GoBytesToPythonBytesLiteral(byteData []byte) string {
	var buffer bytes.Buffer

	for _, singleByte := range byteData {
		buffer.WriteString(goByteToPythonBytesLiteral[singleByte])
	}

	return fmt.Sprintf("b'%s'", buffer.String())
}

// PythonBytesLiteralToGoBytes converts the given Python bytes literal of the
// form `b'...'` or `b"..."` to the corresponding sequence of bytes. For
// example, for the input `b'\x9e\x66\x6f\x6f\x7b\x7d\x0f'` as well as for the
// shorter form `b'\x9efoo{}\x0f'` this function will return [0x9e, 0x66, 0x6f,
// 0x6f, 0x7b, 0x7d, 0x0f].
func PythonBytesLiteralToGoBytes(literal string) ([]byte, error) {
	if len(literal) < 3 {
		return nil, fmt.Errorf(
			"bytes literal must be of the form `b'...'` or `b\"...\"` but got: %q", literal,
		)
	}

	prefix := literal[0:2]
	postfix := literal[len(literal)-1]
	isSingleQuoted := prefix == "b'" && postfix == '\''
	isDoubleQuoted := prefix == "b\"" && postfix == '"'

	if !isSingleQuoted && !isDoubleQuoted {
		return nil, fmt.Errorf(
			"bytes literal must be of the form `b'...'` or `b\"...\"` but got: %q", literal,
		)
	}

	content := literal[2 : len(literal)-1]
	var buffer bytes.Buffer
	pos := 0

	for pos < len(content) {
		if content[pos] == '\\' {
			// Handle escape sequence.
			if pos+1 >= len(content) {
				return nil, fmt.Errorf("unfinished escape sequence at end of: %q", literal)
			}

			switch content[pos+1] {
			case 't':
				buffer.WriteByte('\t')
				pos += 2
			case 'n':
				buffer.WriteByte('\n')
				pos += 2
			case 'r':
				buffer.WriteByte('\r')
				pos += 2
			case '\'':
				buffer.WriteByte('\'')
				pos += 2
			case '"':
				buffer.WriteByte('"')
				pos += 2
			case '\\':
				buffer.WriteByte('\\')
				pos += 2
			case 'x':
				if pos+3 >= len(content) {
					return nil, fmt.Errorf("unfinished escape sequence at end: %q", content[pos:])
				}
				hexString := content[pos+2 : pos+4]
				byteValue, err := strconv.ParseUint(hexString, 16, 8)
				if err != nil {
					return nil, fmt.Errorf(
						"invalid hex escape sequence at pos %d: %q (%w)", pos, content[pos:pos+4], err,
					)
				}
				buffer.WriteByte(byte(byteValue))
				pos += 4
			default:
				return nil, fmt.Errorf("unknown escape sequence at pos %d: %q", pos, content[pos:pos+2])
			}
		} else {
			// No escape sequence.
			if isSingleQuoted && content[pos] == '\'' {
				return nil, fmt.Errorf(
					"single-quoted bytes literal contains unescaped single quote at pos %d", pos,
				)
			}
			if isDoubleQuoted && content[pos] == '"' {
				return nil, fmt.Errorf(
					"double-quoted bytes literal contains unescaped double quote at pos %d", pos,
				)
			}

			buffer.WriteByte(content[pos])
			pos++
		}
	}

	return buffer.Bytes(), nil
}

func allLinesIndented(code string) bool {
	for _, line := range strings.Split(code, "\n") {
		if line != "" && line[0] != ' ' && line[0] != '\t' {
			return false
		}
	}
	return true
}

// checkAnyHasType checks that the type URL in 'paramAny' matches
// 'paramsMessageFullName'.
func checkAnyHasType(anyProto *anypb.Any, messageFullName string) error {
	index := strings.LastIndex(anyProto.GetTypeUrl(), "/")
	if index == -1 {
		return fmt.Errorf("type URL %q does not contain '/'", anyProto.GetTypeUrl())
	}

	anyFullMessageName := anyProto.TypeUrl[index+1:]

	if anyFullMessageName != messageFullName {
		return fmt.Errorf(
			"any proto has type %q but expected %q", anyFullMessageName, messageFullName,
		)
	}

	return nil
}

// extractMessageName extracts, e.g., "foo.bar.MyMessage" -> "MyMessage".
func extractMessageName(messageFullName string) string {
	if strings.Contains(messageFullName, ".") {
		return messageFullName[strings.LastIndex(messageFullName, ".")+1:]
	}
	return messageFullName
}

// toProtoModuleName converts e.g., "foo/bar/baz.proto" to
// ("foo.bar.baz_pb2", "foo.bar", "baz_pb2") or "baz.proto" to
// ("baz_pb2", "", "baz_pb2")
func toProtoModuleName(protoFileName string) (moduleName string, modulePrefix string, moduleAlias string) {
	moduleName = strings.ReplaceAll(
		strings.ReplaceAll(protoFileName, ".proto", "_pb2"), "/", ".",
	)
	if strings.Contains(moduleName, ".") {
		modulePrefix = moduleName[:strings.LastIndex(moduleName, ".")]
		moduleAlias = moduleName[strings.LastIndex(moduleName, ".")+1:]
	} else {
		modulePrefix = ""
		moduleAlias = moduleName
	}
	return
}

type getMessageAndModuleNamesOptions struct {
	fileDescriptorSet          *descriptorpb.FileDescriptorSet
	paramsMessageFullName      string
	returnValueMessageFullName string
}

func getMessageAndModuleNames(options getMessageAndModuleNamesOptions) (*messageAndModuleNames, error) {
	// Find files for parameter and return value message in file descriptor set.
	// For now we only support top-level messages and don't search nested message
	// definitions.
	paramsMessageFileName := ""
	returnValueMessageFileName := ""
	for _, file := range options.fileDescriptorSet.GetFile() {
		for _, messageType := range file.GetMessageType() {
			messageFullName := messageType.GetName()
			if file.GetPackage() != "" {
				messageFullName = file.GetPackage() + "." + messageFullName
			}
			if messageFullName == options.paramsMessageFullName {
				paramsMessageFileName = file.GetName()
			}
			if messageFullName == options.returnValueMessageFullName {
				returnValueMessageFileName = file.GetName()
			}
		}
	}

	if options.paramsMessageFullName != "" && paramsMessageFileName == "" {
		return nil, fmt.Errorf(
			"parameter message %q not found in file descriptor set",
			options.paramsMessageFullName,
		)
	}
	if options.returnValueMessageFullName != "" && returnValueMessageFileName == "" {
		return nil, fmt.Errorf(
			"return value message %q not found in file descriptor set",
			options.returnValueMessageFullName,
		)
	}
	// If both exist, enforce that parameter and return value message are defined
	// in the same file. This is not strictly necessary but simplifies the code
	// generation. We can relax this once required.
	if paramsMessageFileName != "" && returnValueMessageFileName != "" && paramsMessageFileName != returnValueMessageFileName {
		return nil, fmt.Errorf(
			"found parameter message %q in %q and return value message %q in %q in file descriptor set: "+
				" different files for parameter and return value message are not supported",
			options.paramsMessageFullName, paramsMessageFileName,
			options.returnValueMessageFullName, returnValueMessageFileName,
		)
	}

	names := &messageAndModuleNames{
		ParamsMessageName:          extractMessageName(options.paramsMessageFullName),
		ReturnValueMessageName:     extractMessageName(options.returnValueMessageFullName),
		ReturnValueMessageFullName: options.returnValueMessageFullName,
	}

	fileName := paramsMessageFileName
	if fileName == "" {
		fileName = returnValueMessageFileName
	}
	if fileName != "" {
		names.ModuleName, names.ModulePrefix, names.ModuleAlias = toProtoModuleName(fileName)
	}

	return names, nil
}

type messageAndModuleNames struct {
	ModuleName                 string // e.g. "my_folder.my_params_pb2"
	ModulePrefix               string // e.g. "my_folder"
	ModuleAlias                string // e.g. "my_params_pb2"
	ParamsMessageName          string // e.g. "MyParamsMessage"
	ReturnValueMessageName     string // e.g. "MyReturnValueMessage" or ""
	ReturnValueMessageFullName string // e.g. "my_proto_package.MyReturnValueMessage" or ""
}

type templateParams struct {
	// Body of the compute function (including indentation).
	ComputeBody string

	// Python bytes literals, e.g., "b'\x01foo'" (including 'b' and the quotes).
	ParamAnyBytesLiteral          string
	FileDescriptorSetBytesLiteral string

	Names *messageAndModuleNames

	WorldID                string
	WorldServiceAddress    string
	GeometryServiceAddress string
}

// GenerateCodeForExecutionOptions contains the options for
// GenerateCodeForExecution().
type GenerateCodeForExecutionOptions struct {
	FunctionBody string

	ParamAny          *anypb.Any
	FileDescriptorSet *descriptorpb.FileDescriptorSet

	ParameterMessageFullName   string
	ReturnValueMessageFullName string

	WorldID                string
	WorldServiceAddress    string
	GeometryServiceAddress string
}

// GenerateCodeForExecution generates the Python code to be executed on the
// Jupyter server from the parameters of a code execution request.
func GenerateCodeForExecution(options GenerateCodeForExecutionOptions) (string, error) {
	if strings.TrimSpace(options.FunctionBody) == "" {
		return "", fmt.Errorf("function body must not be empty")
	}
	if !allLinesIndented(options.FunctionBody) {
		return "", fmt.Errorf("all lines in function body must be indented")
	}

	var paramsAnyBytes []byte
	if options.ParameterMessageFullName != "" {
		err := checkAnyHasType(options.ParamAny, options.ParameterMessageFullName)
		if err != nil {
			return "", fmt.Errorf("checking type of passed parameter any: %w", err)
		}

		b, err := proto.Marshal(options.ParamAny)
		if err != nil {
			return "", fmt.Errorf("marshaling message: %w", err)
		}
		paramsAnyBytes = b
	}

	fileDescriptorSet := options.FileDescriptorSet
	if fileDescriptorSet == nil {
		fileDescriptorSet = &descriptorpb.FileDescriptorSet{}
	}

	fileDescriptorSetBytes, err := proto.Marshal(fileDescriptorSet)
	if err != nil {
		return "", fmt.Errorf("marshaling message: %w", err)
	}

	names, err := getMessageAndModuleNames(getMessageAndModuleNamesOptions{
		fileDescriptorSet:          fileDescriptorSet,
		paramsMessageFullName:      options.ParameterMessageFullName,
		returnValueMessageFullName: options.ReturnValueMessageFullName,
	})
	if err != nil {
		return "", err
	}

	templateParams := templateParams{
		ComputeBody:                   options.FunctionBody,
		ParamAnyBytesLiteral:          GoBytesToPythonBytesLiteral(paramsAnyBytes),
		FileDescriptorSetBytesLiteral: GoBytesToPythonBytesLiteral(fileDescriptorSetBytes),
		Names:                         names,
		WorldID:                       options.WorldID,
		WorldServiceAddress:           options.WorldServiceAddress,
		GeometryServiceAddress:        options.GeometryServiceAddress,
	}

	var buffer bytes.Buffer
	err = templateSet.ExecuteTemplate(&buffer, executedCodeTemplateName, &templateParams)
	if err != nil {
		return "", fmt.Errorf("executing template %q: %w", executedCodeTemplateName, err)
	}

	return buffer.String(), nil
}

// GenerateCodeForDisplayOptions contains the options for
// GenerateCodeForDisplay().
type GenerateCodeForDisplayOptions struct {
	FunctionBody               string
	FileDescriptorSet          *descriptorpb.FileDescriptorSet
	ParameterMessageFullName   string
	ReturnValueMessageFullName string
}

// GenerateCodeForDisplay generates the Python code to be shown to the user via
// the info service. It is not meant to be executable, but give the user an idea
// in what context the code will be executed.
func GenerateCodeForDisplay(options GenerateCodeForDisplayOptions) (string, error) {
	computeBody := defaultComputeBodyPlaceholder
	if options.FunctionBody != "" {
		if strings.TrimSpace(options.FunctionBody) == "" {
			return "", fmt.Errorf("function body must not only contain whitespace")
		}
		if !allLinesIndented(options.FunctionBody) {
			return "", fmt.Errorf("all lines in function body must be indented")
		}
		computeBody = options.FunctionBody
	}

	fileDescriptorSet := options.FileDescriptorSet
	if fileDescriptorSet == nil {
		fileDescriptorSet = &descriptorpb.FileDescriptorSet{}
	}

	names, err := getMessageAndModuleNames(getMessageAndModuleNamesOptions{
		fileDescriptorSet:          fileDescriptorSet,
		paramsMessageFullName:      options.ParameterMessageFullName,
		returnValueMessageFullName: options.ReturnValueMessageFullName,
	})
	if err != nil {
		return "", err
	}

	templateParams := templateParams{
		ComputeBody: computeBody,
		Names:       names,
	}

	var buffer bytes.Buffer
	err = templateSet.ExecuteTemplate(&buffer, displayedCodeTemplateName, &templateParams)
	if err != nil {
		return "", fmt.Errorf("executing template %q: %w", displayedCodeTemplateName, err)
	}

	return buffer.String(), nil
}

// GenerateCodeForSeeding generates Python code that can be used to seed a
// session before executing code from GenerateCodeForExecution.
func GenerateCodeForSeeding() (string, error) {
	var buffer bytes.Buffer
	err := templateSet.ExecuteTemplate(&buffer, seedCodeTemplateName, nil)
	if err != nil {
		return "", fmt.Errorf("executing template %q: %w", seedCodeTemplateName, err)
	}

	return buffer.String(), nil
}
