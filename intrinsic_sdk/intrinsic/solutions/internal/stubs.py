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

# LINT.IfChange
"""Helpers for generating type stubs for the solution building library."""

import ast
import builtins
import collections
import enum
import inspect
import os
import types
import typing
from typing import Any
from typing import cast
from typing import Optional
from typing import TextIO
from typing import Union

import black
from typing_extensions import override

from intrinsic.math.python import data_types
from intrinsic.solutions import provided
from intrinsic.solutions import providers
from intrinsic.solutions.internal import skill_generation
from intrinsic.solutions.internal import skill_utils

_PYTHON_PATH_SEP = "."
_PYTHON_DOT_OP = "."
_STUBS_SUFFIX = "-stubs"
_ROOT_NAMESPACE_PACKAGE = "intrinsic"

# Import names from the following modules directly, i.e., prefer:
#   from typing import Union
#   Union
# over:
#   import typing
#   typing.Union
_MODULES_WITH_DIRECT_IMPORTS = ("collections.abc", "typing", "types")


# Black has a default column limit of 88 (see
# https://black.readthedocs.io/en/stable/the_black_code_style/current_style.html#line-length).
_FORMATTING_COLUMN_LIMIT = 88


def _print_ast(module: ast.Module) -> str:
  """Converts the given AST to a string and formats it.

  Formatting uses black which is the standard formatter that we also use in the
  dev container.

  Args:
    module: The module AST to print.

  Returns:
    The formatted string representation of the given module.
  """
  code = ast.unparse(ast.fix_missing_locations(module))

  # Black is the formatter we use in the dev container by default.
  code = black.format_str(
      code,
      mode=black.Mode(
          line_length=_FORMATTING_COLUMN_LIMIT,
          is_pyi=True,
      ),
  )

  return code


def _stub_path_for_module(output_path: str, module_name: str) -> str:
  """Returns the path to the stub file for the given module name.

  E.g. for the module name "intrinsic.solutions.providers" returns
  "<output_path>/intrinsic-stubs/solutions/providers.pyi".

  We are generating a stub-only package so we must append "-stubs" to the
  root namespace package. See
  https://peps.python.org/pep-0561/#stub-only-packages.

  Args:
    output_path: The path to the root output directory for the stub files.
    module_name: The name of the module for which to generate the stub.

  Returns:
    The stub file path.
  """
  parts = module_name.split(_PYTHON_PATH_SEP)
  parts[0] += _STUBS_SUFFIX
  parts[-1] += ".pyi"
  return os.path.join(output_path, *parts)


def _make_dirs_and_write_file(path: str, content: str) -> bool:
  """Writes a file with the given content.

  Creates the file if it does not exist and creates parent directories if
  needed.

  Args:
    path: The path of the file to create.
    content: The content to write to the file.

  Returns:
    True if the content of the file changed or if the file was newly created.
  """
  os.makedirs(os.path.dirname(path), exist_ok=True)

  try:
    with open(path, "r+") as file:
      old_content = file.read()
      file.seek(0, os.SEEK_SET)
      file.write(content)
      # Remove any remaining content if new content is shorter than old content.
      file.truncate()
      return content != old_content
  except FileNotFoundError:
    with open(path, "w") as file:
      file.write(content)
    return True


class _AddNodesAfterImports(ast.NodeTransformer):
  """Adds nodes right after the imports at the top of a module."""

  _nodes_to_add: list[ast.AST]

  def __init__(self, nodes_to_add: list[ast.AST]):
    self._nodes_to_add = nodes_to_add

  @override  # pylint: disable-next=invalid-name,missing-function-docstring
  def visit_Module(self, node: ast.Module) -> Any:
    insertion_pos = 0

    # Skip module docstring and imports at the top of the module.
    while (
        isinstance(node.body[insertion_pos], ast.Expr)
        or isinstance(node.body[insertion_pos], ast.Import)
        or isinstance(node.body[insertion_pos], ast.ImportFrom)
    ):
      insertion_pos += 1

    node.body[insertion_pos:insertion_pos] = self._nodes_to_add

    return node


class _RemoveClassFunctions(ast.NodeTransformer):
  """Removes functions from a specific class."""

  _class_name: str
  _functions_to_remove: list[str]

  def __init__(self, class_name: str, functions_to_remove: list[str]):
    self._class_name = class_name
    self._functions_to_remove = functions_to_remove

  def _remove_node(self, node: ast.AST) -> bool:
    return (
        isinstance(node, ast.FunctionDef)
        and node.name in self._functions_to_remove
    )

  @override  # pylint: disable-next=invalid-name,missing-function-docstring
  def visit_ClassDef(self, node: ast.ClassDef) -> Any:
    if node.name == self._class_name:
      node.body = [node for node in node.body if not self._remove_node(node)]
    return node


class _AddNodesAfterClassDocstring(ast.NodeTransformer):
  """Adds nodes after the class docstring of a specific class."""

  _class_name: str
  _nodes_to_add: list[ast.AST]

  def __init__(self, class_name: str, nodes_to_add: list[ast.AST]):
    self._class_name = class_name
    self._nodes_to_add = nodes_to_add

  @override  # pylint: disable-next=invalid-name,missing-function-docstring
  def visit_ClassDef(self, node: ast.ClassDef) -> Any:
    if node.name != self._class_name:
      return node

    insertion_pos = 0

    # Skip class docstring.
    while isinstance(node.body[insertion_pos], ast.Expr):
      insertion_pos += 1

    node.body[insertion_pos:insertion_pos] = self._nodes_to_add

    return node


def _import_contains(
    imp: Union[ast.Import, ast.ImportFrom],
    from_part: Optional[str],
    import_part: str,
):
  if isinstance(imp, ast.Import):
    if from_part is not None:
      return False
    return any(import_part == alias.name for alias in imp.names)
  else:
    return imp.module == from_part and any(
        import_part == alias.name for alias in imp.names
    )


def _add_ast_import(
    imports: list[ast.Import | ast.ImportFrom],
    *,
    module: str,
    name: Optional[str] = None,
) -> str:
  """Adds an import to the given list of imports if it is not already present.

  Adds an import equivalent to the following:
  - If 'name' is given: "from <module> import <name>"
  - If 'name' is not given:
    - If module is a top-level module: "import <module>"
    - If module is a submodule: "from <parent_module> import <module>"
  Does not modify 'imports' if the import is already present.

  Imports for *some* modules are automatically combined (using
  _MODULES_WITH_DIRECT_IMPORTS as an allowlist). For example, "from typing
  import Any" and "from typing import Union" will be combined into a single
  "from typing import Any, Union".

  Args:
    imports: A list of imports to which newly needed imports will be added.
    module: The module or package to import.
    name: The optional name to import. If not given, the module will be imported
      directly.

  Returns:
    The name of the imported module or object under which it can be referenced
    in the code.
  """
  # Convert 'module' + 'name' to 'from_part' + 'import_part' as in:
  #   [from <from_part>] import <import_part>
  from_part: Optional[str]
  import_part: str
  if name is None:
    parts = module.split(_PYTHON_DOT_OP)
    if len(parts) == 1:
      from_part = None
      import_part = parts[0]
    else:
      from_part = _PYTHON_DOT_OP.join(parts[:-1])
      import_part = parts[-1]
  else:
    from_part = module
    import_part = name

  import_already_exists = any(
      _import_contains(imp, from_part, import_part) for imp in imports
  )
  if import_already_exists:
    return import_part

  if from_part is not None and from_part in _MODULES_WITH_DIRECT_IMPORTS:
    existing_import_for_module = cast(
        Optional[ast.ImportFrom],
        next(
            filter(
                lambda imp: isinstance(imp, ast.ImportFrom)
                and imp.module == from_part,
                imports,
            ),
            None,
        ),
    )
    if existing_import_for_module is not None:
      # Add to the existing import to get the combined import:
      #   from <from_part> import <existing_names>, <import_part>
      existing_import_for_module.names.append(ast.alias(name=import_part))
      existing_import_for_module.names.sort(key=lambda alias: alias.name)
      return import_part

  # Add completely new import.
  if from_part is None:
    imports.append(ast.Import(names=[ast.alias(name=import_part)], level=0))
  else:
    imports.append(
        ast.ImportFrom(
            module=from_part, names=[ast.alias(name=import_part)], level=0
        )
    )

  return import_part


def dot_expr_to_ast_attribute_or_name(
    dot_expr: str,
) -> Union[ast.Attribute, ast.Name]:
  """Converts the given "dot expression" to an ast.Attribute or ast.Name.

  Examples:
    - For the expression "a" returns ast.Name(id="a")
    - For the nested expression "a.b.c" returns
          ast.Attribute(
              value=ast.Attribute(
                  value=ast.Name(id="a"),
                  attr="b"),
              attr="c")

  Args:
    dot_expr: The (potentially nested) dot expression such as "a.b.c", "a.b" or
      "a".

  Returns:
    An ast.Attribute or ast.Name which is equivalent to the given dot
    expression.
  """
  parts = dot_expr.split(_PYTHON_DOT_OP)
  result = ast.Name(id=parts[0], ctx=ast.Load())
  for part in parts[1:]:
    result = ast.Attribute(value=result, attr=part, ctx=ast.Load())
  return result


def _skill_provider_typedef_for_skill_class(
    skill_class: type[provided.SkillBase],
) -> ast.Assign:
  """Returns an ast.Assign for the given skill class.

  The returned ast.Assign is equivalent to, e.g.:
    move_robot = intrinsic.solutions.skills.ai.intrinsic.move_robot

  To be used inside body of the SkillProvider class.

  Args:
    skill_class: The skill class for which to generate the ast.Assign.

  Returns:
    The created ast.Assign object.
  """
  return ast.Assign(
      targets=[ast.Name(id=skill_class.__name__, ctx=ast.Store())],
      value=dot_expr_to_ast_attribute_or_name(
          f"{skill_class.__module__}{_PYTHON_DOT_OP}{skill_class.__qualname__}"
      ),
  )


def _skill_provider_classdef_for_skill_package(
    skill_package: provided.SkillPackage,
) -> ast.ClassDef:
  """Returns an ast.ClassDef for the given skill package.

  The returned ast.ClassDef is equivalent to, e.g.:
    class ai:
      <docstring>
      <recursively generated typedefs and classes for the skill package "ai">

  To be used inside the body of the SkillProvider class.

  Args:
    skill_package: The skill package for which to generate the ast.ClassDef.

  Returns:
    The created ast.ClassDef object.
  """
  class_docstring = ast.Expr(
      value=ast.Constant(
          value=(
              "Namespace class for the skill package"
              f" '{skill_package.package_name}'.\n\nContains the skills and"
              " child skill packages of the skill package"
              f" '{skill_package.package_name}'.\n\nThis class cannot be"
              " instantiated.\n"
          )
      )
  )

  return ast.ClassDef(  # pytype: disable=missing-parameter
      name=skill_package.relative_package_name,
      bases=[],
      keywords=[],
      body=(
          [class_docstring]
          + _skill_provider_typedefs_and_classdefs_for_skill_container(
              skill_package
          )
      ),
      decorator_list=[],
  )


def _sort_ast_typedefs_and_classdefs(
    nodes: list[Union[ast.ClassDef, ast.Assign]],
) -> None:
  """Sorts the given list of typedefs and classes.

  Sorts such that the typedefs come first (in lexicographical order), followed
  by the classes (in lexicographical order).

  Args:
    nodes: The list of typedefs and classes to sort.
  """
  nodes.sort(
      key=lambda node: ("1", node.name)
      if isinstance(node, ast.ClassDef)
      else ("0", node.targets[0].id)
  )


def _skill_provider_typedefs_and_classdefs_for_skill_container(
    skill_container: Union[provided.SkillPackage, providers.SkillProvider],
) -> list[Union[ast.ClassDef, ast.Assign]]:
  """Returns typedefs (ast.Assign) and ClassDefs for the given skill container.

  E.g. if the skill container is a skill provider and has an attribute
  "global_skill" (a skill class for a skill without a skill package) and an
  attribute "ai" (a child skill package), the returned list is equivalent to:

    global_skill = intrinsic.solutions.skills.global_skill
    class ai:
      <recursively generated typedefs and classes for the skill package "ai">

  To be used inside the body of the SkillProvider class.

  Args:
    skill_container: A skill package or skill provider - anything for which
      getattr() can return a skill class or a skill package.

  Returns:
    A list of ast.Assign and ast.ClassDef nodes sorted by type (primary) and
    name (secondary).
  """
  nodes: list[Union[ast.ClassDef, ast.Assign]] = []
  for name in dir(skill_container):
    attribute = getattr(skill_container, name)

    if isinstance(attribute, provided.SkillPackage):
      nodes.append(_skill_provider_classdef_for_skill_package(attribute))
    elif inspect.isclass(attribute) and issubclass(
        attribute, provided.SkillBase
    ):
      nodes.append(_skill_provider_typedef_for_skill_class(attribute))

  _sort_ast_typedefs_and_classdefs(nodes)

  return nodes


def _generate_providers_stub(
    output_path: str, skills: providers.SkillProvider
) -> bool:
  """Generates and writes the stub for the 'providers' module.

  Args:
    output_path: The path to the root output directory for the stub files.
    skills: The SkillProvider which provides the skills for which to generate
      stubs.

  Returns:
    True if the content of the stub file changed or if the file was newly
    created.
  """

  with open(providers.__file__, "r") as file:
    # ast.parse/unparse does not preserve comments but it preserves docstrings
    # (since docstrings are string expressions).
    root = ast.parse(file.read())

  # Add imports for skill modules (other pyi-files).
  unique_modules = set(skill.__module__ for skill in skills.get_skill_classes())
  imports_to_add = [
      ast.Import(names=[ast.alias(name=module)])
      for module in sorted(unique_modules)
  ]
  _AddNodesAfterImports(imports_to_add).visit(root)

  # Remove SkillProvider.__dir__ and SkillProvider.__getattr__. Their
  # functionality gets replaced by explicit class attributes and nested classes.
  _RemoveClassFunctions("SkillProvider", ["__dir__", "__getattr__"]).visit(root)

  # Add a class attribute SkillProvider.<skill_name> for each global skill and a
  # nested class SkillProvider.<skill_package_name> for each skill package.
  _AddNodesAfterClassDocstring(
      "SkillProvider",
      _skill_provider_typedefs_and_classdefs_for_skill_container(skills),
  ).visit(root)

  providers_content = _print_ast(root)

  stub_path = _stub_path_for_module(output_path, providers.__name__)
  return _make_dirs_and_write_file(stub_path, providers_content)


def _generate_py_typed(output_path: str) -> bool:
  r"""Generates and writes an appropriate 'py.typed' file.

  The 'py.typed' file has the content 'partial\n' and is required to mark the
  generated stub package as partial (we don't generate stubs for all modules).
  See https://peps.python.org/pep-0561/#partial-stub-packages.

  Args:
    output_path: The path to the root output directory for the stub files.

  Returns:
    True if the content of 'py.typed' changed or if the file was newly created.
  """
  # The location of the 'py.typed' file currently does not fully follow PEP561
  # because this is not supported by pylance.
  # According to PEP561, 'py.typed' file(s) should be in the "submodules of the
  # namespace", so something like:
  #   - "intrinsic-stubs/intrinsic/solutions/py.typed"
  #   - "intrinsic-stubs/intrinsic/solutions/providers.pyi"
  #   - "intrinsic-stubs/intrinsic/solutions/skills/ai/intrinsic/py.typed"
  #   - "intrinsic-stubs/intrinsic/solutions/skills/ai/intrinsic/__init__.pyi"
  # We are currently generating only a single, global 'py.typed' file which is
  # the only way that is supported by pylance:
  #   - "intrinsic-stubs/py.typed"
  #   - "intrinsic-stubs/intrinsic/solutions/providers.pyi"
  #   - "intrinsic-stubs/intrinsic/solutions/skills/ai/intrinsic/__init__.pyi"
  # This might be fixed in Pylance in the future, see
  # https://github.com/microsoft/pylance-release/issues/5508.
  py_typed_path = os.path.join(
      output_path, _ROOT_NAMESPACE_PACKAGE + _STUBS_SUFFIX, "py.typed"
  )

  return _make_dirs_and_write_file(py_typed_path, "partial\n")


def _union_args_to_ast_expressions(
    union_args: Any,
    module_name: str,
    imports: list[ast.Import | ast.ImportFrom],
) -> list[ast.expr]:
  """Converts the given Union args to ast expressions.

  Converts the given Union args (obtained with typing.get_args()) to a list of
  equivalent ast expressions. Performs replacements to improve the readability
  and conciseness of the generated stub code, such as replacing [...,
  BlackboardValue, CelExpression, ...] with [..., ParamAssignment, ...]. Note
  that we need to do this manually since, even if "ParamAssignment" was used to
  generate a signature, the resulting signature object will contain a
  "Union[..., BlackboardValue, CelExpression, ...]".

  Args:
    union_args: The args of a Union obtained with typing.get_args().
    module_name: The name of the module in which the resulting AST will be used.
    imports: A list of imports to which newly needed imports will be added.

  Returns:
    A list of ast expressions equivalent to the given Union args.
  """
  param_assignment_insert_pos = None
  param_assignment_types = set(typing.get_args(provided.ParamAssignment))
  if param_assignment_types.issubset(union_args):
    # Remove BlackboardValue & CelExpression before the recursive calls to
    # _annotation_to_ast_expr() below to avoid side-effects such as imports
    # getting added for BlackboardValue & CelExpression. Save removal position
    # for inserting ParamAssignment later at the same position.
    param_assignment_insert_pos = min(
        union_args.index(type) for type in param_assignment_types
    )
    union_args = [
        arg for arg in union_args if arg not in param_assignment_types
    ]

  ast_args = [
      _annotation_to_ast_expr(arg, module_name, imports) for arg in union_args
  ]

  if param_assignment_insert_pos is not None:
    name = _add_ast_import(
        imports, module=provided.__name__, name="ParamAssignment"
    )
    ast_args.insert(
        param_assignment_insert_pos, ast.Name(id=name, ctx=ast.Load())
    )

  return ast_args


def _annotation_to_ast_expr(
    annotation: Any,
    module_name: str,
    imports: list[ast.Import | ast.ImportFrom],
) -> ast.expr:
  """Converts the given type annotation to an ast.expr.

  Adds imports for any modules that are used in the returned expression.

  Args:
    annotation: The type annotation taken from an inspect.Parameter object.
    module_name: The name of the module in which the resulting AST will be used.
    imports: A list of imports to which newly needed imports will be added.

  Returns:
    An ast.expr expression equivalent to the given type annotation.
  """
  if isinstance(annotation, str):
    # Handle post-evaluated type annotations as in, e.g., "self: 'MyClass'".
    return ast.Constant(value=annotation)
  elif (origin := typing.get_origin(annotation)) and (
      args := typing.get_args(annotation)
  ):
    # Handle generic aliases such as "annotation = list[int]". These mainly
    # appear as instances of types.GenericAlias or typing._GenericAlias but
    # there are other special cases. typing.get_origin()/get_args() seems to be
    # the most general way to catch all cases. For example, for
    # "annotation = dict[int, str]", "typing.get_origin(annotation)"
    # will return <class 'dict'> and "typing.get_args(annotation)" will return
    # (<class 'int'>, <class 'str'>) which we can handle recursively.

    # Special case: Print "<arg0> | <arg1> | ..." instead of "Union[<args>]".
    if origin is typing.Union:
      ast_args = _union_args_to_ast_expressions(args, module_name, imports)

      result = ast_args[0]  # A Union always has at least one arg.
      for ast_arg in ast_args[1:]:
        result = ast.BinOp(left=result, op=ast.BitOr(), right=ast_arg)
      return result

    # Default case: Print "<origin>[<args>]".
    ast_origin = _annotation_to_ast_expr(origin, module_name, imports)
    ast_args = [
        _annotation_to_ast_expr(arg, module_name, imports) for arg in args
    ]

    return ast.Subscript(
        value=ast_origin,
        slice=(
            ast_args[0]
            if len(ast_args) == 1
            else ast.Tuple(elts=ast_args, ctx=ast.Load())
        ),
    )
  elif annotation in [typing.Union, typing.Optional, typing.Any]:
    # Handle special case for some types from the typing module. Depending on
    # the Python version, typing.Union/Optional/Any are not class objects and
    # the general "inspect.isclass(annotation)" case below won't catch them.
    # Note that "typing.Union" is distinct from "typing.Union[x, y]". This case
    # here will trigger for "annotation = typing.Union" but not for a "whole"
    # annotation such as "annotation = typing.Union[x, y]". In the latter case
    # we will first decompose the annotation into its parts: in the case above
    # "get_origin(annotation)" will return "typing.Union" for which we then call
    # this function recursively. Then this case here will trigger.
    name = _add_ast_import(imports, module="typing", name=annotation.__name__)
    return ast.Name(id=name, ctx=ast.Load())
  elif inspect.isclass(annotation):
    # Handle annotations that point to class objects.
    if annotation.__module__ == builtins.__name__:
      # Built-in types don't need an import and their name can simply be used
      # without a module prefix. Special case: Instead of "NoneType" we can
      # write "None".
      corrected_name = (
          "None" if annotation.__name__ == "NoneType" else annotation.__name__
      )
      return ast.Name(id=corrected_name, ctx=ast.Load())
    elif annotation.__module__ in _MODULES_WITH_DIRECT_IMPORTS:
      # The annotated type is from a module for which we want to import all
      # names directly. For example, add "from typing import Any" and use "Any".
      name = _add_ast_import(
          imports, module=annotation.__module__, name=annotation.__name__
      )
      return ast.Name(id=name, ctx=ast.Load())
    elif annotation.__module__ == module_name:
      # The annotated type is from the module whose code we are generating. We
      # don't need to add an import and can use the qualified name (=name
      # relative to module root). For example, we return
      # "move_robot.intrinsic_proto.Pose".
      return dot_expr_to_ast_attribute_or_name(annotation.__qualname__)
    elif annotation is data_types.Pose3:
      # The annotated type equals the type pointed to by "data_types.Pose3"
      # (== "pose3.Pose3"), use "data_types.Pose3" instead.
      name = _add_ast_import(imports, module=data_types.__name__)
      return dot_expr_to_ast_attribute_or_name(name + _PYTHON_DOT_OP + "Pose3")
    else:
      # The annotated type is from a non-special module: Add an import for that
      # module and use its qualified name. For example, add
      # "from foo import bar" and use "bar.Baz".
      name = _add_ast_import(imports, module=annotation.__module__)
      return dot_expr_to_ast_attribute_or_name(
          name + _PYTHON_DOT_OP + annotation.__qualname__
      )

  # Return 'Any' as a reasonable default for many cases (but not all).
  name = _add_ast_import(imports, module="typing", name="Any")
  return ast.Name(id=name, ctx=ast.Load())


def _param_to_ast_arg(
    param: inspect.Parameter,
    module_name: str,
    imports: list[ast.Import | ast.ImportFrom],
) -> tuple[ast.arg, Optional[ast.Expr]]:
  """Converts the given inspect.Parameter to corresponding ast nodes.

  Adds imports for any modules that are used in the returned nodes.

  Args:
    param: The inspect.Parameter to convert.
    module_name: The name of the module in which the resulting AST will be used.
    imports: A list of imports to which newly needed imports will be added.

  Returns:
    A tuple whose first element is an ast.arg node which represents the
    parameter name and type annotation and whose second element is an ast.Expr
    which represents the parameter's default value if present or else None.
  """
  # Do not annotate "self" parameters, this is not required.
  annotation = (
      _annotation_to_ast_expr(param.annotation, module_name, imports)
      if param.annotation and param.name != "self"
      else None
  )
  default_value = None
  if param.default != inspect.Parameter.empty:
    # We get the default value as an instance/value object and basically need to
    # generate code that would create this instance. For now we just use
    # Ellipsis for all cases except empty lists/dicts. Ellipses for default
    # values are legal and common in stub files.
    if param.default == []:  # pylint: disable=g-explicit-bool-comparison
      default_value = ast.Expr(value=ast.List(elts=[], ctx=ast.Load()))
    elif param.default == {}:  # pylint: disable=g-explicit-bool-comparison
      default_value = ast.Expr(value=ast.Dict(keys=[], values=[]))
    else:
      default_value = ast.Expr(value=ast.Constant(value=Ellipsis))

  return ast.arg(arg=param.name, annotation=annotation), default_value


def _function_to_ast_function_def(
    name: str,
    func: types.FunctionType,
    module_name: str,
    imports: list[ast.Import | ast.ImportFrom],
) -> ast.FunctionDef:
  """Generates an ast.FunctionDef for the given function object.

  Args:
    name: The name of the function for which a definition should be generated.
    func: The function object for which a definition should be generated.
    module_name: The name of the module in which the resulting AST will be used.
    imports: A list of imports to which newly needed imports will be added.

  Returns:
    An ast.FunctionDef object which represents the function definition.
  """
  docstring = ast.Expr(value=ast.Constant(value=func.__doc__))

  signature = inspect.signature(func)

  args = []
  kwonlyargs = []
  kw_defaults = []
  for param in signature.parameters.values():
    ast_arg, ast_default = _param_to_ast_arg(param, module_name, imports)

    if param.kind == inspect.Parameter.POSITIONAL_OR_KEYWORD:
      args.append(ast_arg)
    elif param.kind == inspect.Parameter.KEYWORD_ONLY:
      kwonlyargs.append(ast_arg)
      kw_defaults.append(ast_default)

  return ast.FunctionDef(  # pytype: disable=missing-parameter
      name=name,
      args=ast.arguments(
          # Our skill and message wrapper classes don't use position-only-args.
          posonlyargs=[],
          args=args,
          kwonlyargs=kwonlyargs,
          kw_defaults=kw_defaults,
          # Defaults for 'posonlyargs' and 'args', not used by our skill and
          # message wrapper classes.
          defaults=[],
      ),
      body=[docstring, ast.Expr(value=ast.Constant(value=Ellipsis))],
      decorator_list=[],
  )


def _ast_assign_for_enum_value_shortcut(
    name: str, enum_value: enum.IntEnum
) -> ast.Assign:
  """Returns an ast.Assign for a shortcut to the given enum value.

  The returned ast.Assign is equivalent to, e.g.:
    MOTION_TYPE_JOINT = move_robot.intrinsic_proto.MotionType.MOTION_TYPE_JOINT

  To be used inside the body of a class definition for a skill class (see
  _ast_typedefs_and_classdefs_for_message_wrapper_container()).

  Args:
    name: The name to which to assign to.
    enum_value: The enum value object.

  Returns:
    The created ast.Assign object.
  """
  return ast.Assign(
      targets=[ast.Name(id=name, ctx=ast.Store())],
      value=dot_expr_to_ast_attribute_or_name(
          f"{type(enum_value).__qualname__}{_PYTHON_DOT_OP}{enum_value.name}"
      ),
  )


def _ast_class_def_for_enum_wrapper(
    enum_type: type[enum.IntEnum],
    imports: list[ast.Import | ast.ImportFrom],
) -> ast.ClassDef:
  """Returns an ast.ClassDef for the given enum wrapper class.

  To be used inside the body of a class definition for a skill class (see
  _ast_typedefs_and_classdefs_for_message_wrapper_container()).

  Args:
    enum_type: The enum wrapper class for which to generate the ast.ClassDef.
    imports: A list of imports to which newly needed imports will be added.

  Returns:
    An ast.ClassDef object which represents the message wrapper class.
  """
  enum_value_defs = [
      ast.Assign(
          targets=[ast.Name(id=value.name, ctx=ast.Store())],
          value=ast.Constant(value=value.value),
      )
      for value in enum_type
  ]

  # Add "import enum"
  enum_module_name = _add_ast_import(imports, module=enum.__name__)

  return ast.ClassDef(  # pytype: disable=missing-parameter
      name=enum_type.__name__,
      bases=[
          # Declare "enum.IntEnum" as base class
          dot_expr_to_ast_attribute_or_name(
              f"{enum_module_name}{_PYTHON_DOT_OP}{enum.IntEnum.__name__}"
          )
      ],
      keywords=[],
      body=enum_value_defs,
      decorator_list=[],
  )


def _ast_class_def_for_message_wrapper_namespace(
    message_wrapper_namespace: type[skill_utils.MessageWrapperNamespace],
    module_name: str,
    imports: list[ast.Import | ast.ImportFrom],
) -> ast.ClassDef:
  """Returns an ast.ClassDef for the given message wrapper namespace class.

  To be used inside the body of a class definition for a skill class (see
  _ast_typedefs_and_classdefs_for_message_wrapper_container()).

  Args:
    message_wrapper_namespace: The message wrapper namespace class for which to
      generate the ast.ClassDef.
    module_name: The name of the module in which the resulting AST will be used.
    imports: A list of imports to which newly needed imports will be added.

  Returns:
    An ast.ClassDef object which represents the message wrapper class.
  """
  class_docstring = ast.Expr(
      value=ast.Constant(value=message_wrapper_namespace.__doc__)
  )

  return ast.ClassDef(  # pytype: disable=missing-parameter,wrong-arg-types
      name=message_wrapper_namespace.__name__,
      # Don't declare skill_utils.MessageWrapperNamespace as base class in the
      # stub. This class is an internal implementation helper and does not
      # provide any useful functionality that a user would care about.
      bases=(),
      keywords=[],
      body=[class_docstring]
      + _ast_typedefs_and_classdefs_for_message_wrapper_container(
          message_wrapper_namespace, module_name, imports
      ),
      decorator_list=[],
  )


def _ast_class_def_for_message_wrapper(
    message_wrapper: type[skill_utils.MessageWrapper],
    module_name: str,
    imports: list[ast.Import | ast.ImportFrom],
) -> ast.ClassDef:
  """Returns an ast.ClassDef for the given message wrapper class.

  To be used inside the body of a class definition for a skill class (see
  _ast_typedefs_and_classdefs_for_message_wrapper_container()).

  Args:
    message_wrapper: The message wrapper class for which to generate the
      ast.ClassDef.
    module_name: The name of the module in which the resulting AST will be used.
    imports: A list of imports to which newly needed imports will be added.

  Returns:
    An ast.ClassDef object which represents the message wrapper class.
  """
  class_docstring = ast.Expr(value=ast.Constant(value=message_wrapper.__doc__))
  init_def = _function_to_ast_function_def(
      "__init__", message_wrapper.__init__, module_name, imports
  )

  class_body = [
      class_docstring,
      init_def,
  ] + _ast_typedefs_and_classdefs_for_message_wrapper_container(
      message_wrapper, module_name, imports
  )

  return ast.ClassDef(  # pytype: disable=missing-parameter
      name=message_wrapper.__name__,
      bases=[],
      keywords=[],
      body=class_body,
      decorator_list=[],
  )


def _ast_typedefs_and_classdefs_for_message_wrapper_container(
    container: Union[
        type[provided.SkillBase],
        type[skill_utils.MessageWrapperNamespace],
        type[skill_utils.MessageWrapper],
    ],
    module_name: str,
    imports: list[ast.Import | ast.ImportFrom],
) -> list[ast.ClassDef | ast.Assign]:
  """Returns typedefs and class defs for the given message wrapper container.

  For example, if the container is a skill class "my_skill" which has an
  attribute "intrinsic_proto" (a message wrapper namespace class), the returned
  list is equivalent to:

    class intrinsic_proto:
      <recursively generated typedefs and class defs for the message wrappers of
      the proto package "intrinsic_proto">

  Or, if the container is a namespace class for the proto package
  "intrinsic_proto" which has an attribute "Pose" (a message wrapper class) and
  an attribute "world" (a message wrapper namespace class), the returned list is
  equivalent to:

    class Pose:
      <class definition of the message wrapper for "intrinsic_proto.Pose">
    class world:
      <recursively generated typedefs and class defs for the message wrappers of
      the proto package "intrinsic_proto.world">

  To be used inside the body of a class definition for a skill class.

  Args:
    container: A skill class, message wrapper class or a message wrapper
      namespace class - anything for which getattr() can return a message
      wrapper class or a message wrapper namespaceclass.
    module_name: The name of the module in which the resulting AST will be used.
    imports: A list of imports to which newly needed imports will be added.

  Returns:
    A list of ast.Assign and ast.ClassDef nodes sorted by type (primary) and
    name (secondary).
  """
  nodes: list[ast.ClassDef | ast.Assign] = []

  for name in dir(container):
    attribute = getattr(container, name)

    if isinstance(attribute, enum.IntEnum):
      nodes.append(_ast_assign_for_enum_value_shortcut(name, attribute))
      continue

    if not inspect.isclass(attribute):
      continue

    if issubclass(attribute, enum.IntEnum):
      nodes.append(_ast_class_def_for_enum_wrapper(attribute, imports))
    elif issubclass(attribute, skill_utils.MessageWrapperNamespace):
      nodes.append(
          _ast_class_def_for_message_wrapper_namespace(
              attribute, module_name, imports
          )
      )
    elif issubclass(attribute, skill_utils.MessageWrapper):
      message_wrapper = cast(type[skill_utils.MessageWrapper], attribute)
      nodes.append(
          _ast_class_def_for_message_wrapper(
              message_wrapper, module_name, imports
          )
      )

  _sort_ast_typedefs_and_classdefs(nodes)

  return nodes


def _ast_module_stub_for_skill_package(
    skill_package_name: str,
    module_name: str,
    skills: list[type[provided.SkillBase]],
) -> ast.Module:
  """Generates a stub module for the given skill package and its skills.

  The module contains a class definition for each skill in the package and
  nested within each skill class definition there are definitions for the skills
  message wrapper classes.

  Args:
    skill_package_name: The name of the skill package (e.g. 'ai.intrinsic').
    module_name: The name of the generated module  (e.g.
      'intrinsic.solutions.skills.ai.intrinsic').
    skills: The skills in the skill package.

  Returns:
    An ast.Module node representing the stub file for the skill package.
  """
  skill_list = "\n".join(
      f" - {skill.skill_info.skill_name}" for skill in skills
  )
  module_docstring = ast.Expr(
      value=ast.Constant(
          value=(
              "Skill classes for the skills in the skill package"
              f" '{skill_package_name}'.\n\nContains class definitions for the"
              f" following skills:\n{skill_list}\n"
          )
      )
  )

  imports: list[ast.Import | ast.ImportFrom] = []
  # Allow forward references in type annotations.
  _add_ast_import(imports, module="__future__", name="annotations")
  class_defs = []

  for skill in skills:
    class_docstring = ast.Expr(value=ast.Constant(value=skill.__doc__))
    init_def = _function_to_ast_function_def(
        "__init__", skill.__init__, module_name, imports
    )
    message_wrapper_class_defs = (
        _ast_typedefs_and_classdefs_for_message_wrapper_container(
            skill, module_name, imports
        )
    )

    class_body = [class_docstring, init_def] + message_wrapper_class_defs

    # Add, e.g., "from intrinsic.solutions.internal import skill_generation".
    skill_generation_name = _add_ast_import(
        imports, module=skill_generation.__name__
    )
    class_defs.append(
        ast.ClassDef(  # pytype: disable=missing-parameter
            name=skill.__name__,
            bases=[
                dot_expr_to_ast_attribute_or_name(
                    skill_generation_name
                    + _PYTHON_DOT_OP
                    + skill_generation.GeneratedSkill.__name__
                )
            ],
            keywords=[],
            body=class_body,
            decorator_list=[],
        )
    )

  module_body = [
      module_docstring,
      imports,
  ] + class_defs

  return ast.Module(body=module_body, type_ignores=[])


def _generate_skill_module_stubs(
    output_path: str, skills: providers.SkillProvider
) -> bool:
  """Generates and writes stubs for the skill modules.

  For each skill package provided by the given SkillProvider generates and
  writes a corrensponding module stub which contains class definitions for each
  skill in that package.

  For example, if the provided skills are:
    - global_skill
    - ai.intrinsic.move_robot
    - ai.intrinsic.estimate_pose
  Then this function generates and writes:
    - intrinsic-stubs/intrinsic/solutions/skills/ai/intrinsic/__init__.pyi
      containing class definitions for 'move_robot' and 'estimate_pose'.
    - intrinsic-stubs/intrinsic/solutions/skills/__init__.pyi
      containing a class definition for 'global_skill'.

  Args:
    output_path: The path to the root output directory for the stub files.
    skills: The SkillProvider which provides the skills for which to generate
      stubs.

  Returns:
    True if the content of any file changed or if any file was newly created.
  """

  skills_by_package: dict[str, list[type[provided.SkillBase]]] = (
      collections.defaultdict(list)
  )

  for skill in skills.get_skill_classes():
    skills_by_package[skill.skill_info.package_name].append(skill)

  any_file_changed = False
  for skill_package_name, skills in skills_by_package.items():
    skills.sort(key=lambda skill: skill.skill_info.skill_name)
    module_name = skill_utils.module_for_generated_skill(skill_package_name)
    ast_module = _ast_module_stub_for_skill_package(
        skill_package_name, module_name, skills
    )
    content = _print_ast(ast_module)
    stub_path = _stub_path_for_module(
        output_path, module_name + _PYTHON_PATH_SEP + "__init__"
    )
    file_changed = _make_dirs_and_write_file(stub_path, content)
    any_file_changed = any_file_changed or file_changed
  return any_file_changed


def _running_in_vscode() -> bool:
  # VSCODE_CWD is set if running a notebook in VSCode.
  # TERM_PROGRAM=vscode is set if running a python script on a VSCode terminal.
  return "VSCODE_CWD" in os.environ or (
      "TERM_PROGRAM" in os.environ and os.environ["TERM_PROGRAM"] == "vscode"
  )


def generate(
    output_path: str, skills: providers.SkillProvider, stdout: TextIO
) -> None:
  """Generates type stubs for all skills provided by the given skill provider.

  This is the implementation of deployments.Solution.generate_stubs(). For more
  details also see that method.

  Args:
    output_path: The path to the root output directory for the stub files.
    skills: The skill provider of a solution for which to generate stubs.
    stdout: Output stream to which to write status messages.
  """
  any_file_changed = any([
      _generate_providers_stub(output_path, skills),
      _generate_py_typed(output_path),
      _generate_skill_module_stubs(output_path, skills),
  ])

  if any_file_changed:
    if _running_in_vscode():
      stdout.write(
          f"The stubs in {os.path.abspath(output_path)} have been successfully"
          " updated. You might need to reload the VS Code window or run the"
          ' "Python: Restart Language Server" command for the changes to take'
          " effect."
      )
    else:
      stdout.write(
          f"The stubs in {os.path.abspath(output_path)} have been successfully"
          " updated. If necessary, restart your IDE, type-checker or language"
          " server for the changes to take effect."
      )
  else:
    stdout.write(
        f"The stubs in {os.path.abspath(output_path)} are already up-to-date."
    )


# LINT.ThenChange(//incode/solutions/skills/skill_module_example.py)
