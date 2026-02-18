import json
import os
from pathlib import Path
from typing import Dict, List, Tuple

# -------------------------
# Inputs (from your file)
# -------------------------

COPY_FROM_PATH = r"PCGRuntimeUtilities\Source\ISMRuntimeCore"  # unused for now

TARGET_ABSOLUTE_PATH_UPLUGIN_FILE = r"B:\UnrealEngine5_Projects\_repos5\ProceduralPlugins\Plugins\PCGRuntimeUtilities\PCGRuntimeUtils.uplugin"
TARGET_ABSOLUTE_PATH_SOURCE = r"B:\UnrealEngine5_Projects\_repos5\ProceduralPlugins\Plugins\PCGRuntimeUtilities\Source"

DEFAULT_PUBLIC_DEPENDENCIES = ["Core", "CoreUObject", "Engine", "ISMRuntimeCore", "GameplayTags"]
DEFAULT_PRIVATE_DEPENDENCIES: List[str] = []

# format: ModuleName : [IsRuntimeModule, Description, PublicDependencies, PrivateDependencies]
OUTPUT_MODULE_DEFINITIONS: Dict[str, List] = {
    "ISMRuntimePools":        [True,  "", [], []],
    "ISMRuntimeSpatial":      [True,  "", [], []],
    "ISMRuntimeResource":     [True,  "", [], []],
    "ISMRuntimePhysics":      [True,  "", ["ISMRuntimePools", "PhysicsCore", "ISMRuntimeFeedbacks"], []],
    "ISMRuntimeInteraction":  [True,  "", ["ISMRuntimeSpatial"], ["UMG", "Slate", "SlateCore"]],
    "ISMRuntimeDebug":        [False, "", [], ["ISMRuntimePools", "ISMRuntimeSpatial", "ISMRuntimeResource", "ISMRuntimePhysics", "ISMRuntimeInteraction", "ISMRuntimeDamage", "ISMRuntimeFeedbacks"]],
    # NOTE: you had a missing comma between "ISMRuntimeInteraction" and "ISMRuntimeDamage" in your uploaded file.
    "ISMRuntimeEditor":       [False, "", ["UnrealEd", "ISMRuntimePools", "ISMRuntimeSpatial", "ISMRuntimeResource", "ISMRuntimePhysics", "ISMRuntimeInteraction", "ISMRuntimeDamage"], ["UMG", "Slate", "SlateCore"]],
    "ISMRuntimeDamage":       [True,  "", ["ISMRuntimeFeedbacks"], []],
    "ISMRuntimeCoreTests":        [True,  "", [], []],
    "ISMRuntimeFeedbacks":       [True,  "", [], ["Niagara"]],
    "ISMRuntimeDestruction":       [True,  "", ["ISMRuntimePools", "GeometryCollectionEngine"], []],
}

# -------------------------
# Templates (fixed)
# -------------------------

BUILD_CS_TEMPLATE = """// Copyright Max Harris

using UnrealBuildTool;

public class {ModuleName} : ModuleRules
{{
    public {ModuleName}(ReadOnlyTargetRules Target) : base(Target)
    {{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {{
{PublicDeps}
            }}
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {{
{PrivateDeps}
            }}
        );
    }}
}}
"""

MODULE_HEADER_TEMPLATE = """#pragma once

#include "Modules/ModuleManager.h"

class F{ModuleName} : public IModuleInterface
{{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
}};
"""

MODULE_CPP_TEMPLATE = """#include "{ModuleName}.h"

#define LOCTEXT_NAMESPACE "F{ModuleName}Module"

void F{ModuleName}::StartupModule()
{{
}}

void F{ModuleName}::ShutdownModule()
{{
}}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(F{ModuleName}, {ModuleName})
"""

# -------------------------
# Helpers
# -------------------------

def _dedupe_preserve_order(items: List[str]) -> List[str]:
    seen = set()
    out = []
    for x in items:
        if x and x not in seen:
            seen.add(x)
            out.append(x)
    return out

def _format_cs_string_array(items: List[str], indent: str = "                ") -> str:
    # Produces:
    #                 "Core",
    #                 "Engine",
    lines = [f'{indent}"{x}",' for x in items]
    return "\n".join(lines)

def _module_type(module_name: str, is_runtime: bool) -> str:
    if is_runtime:
        return "Runtime"
    # heuristic: Editor modules should be Type="Editor"
    if module_name.endswith("Editor"):
        return "Editor"
    # Debug/tooling modules often fit as Developer
    return "Developer"

def _ensure_list_strings(value, err_prefix: str) -> List[str]:
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(x, str) for x in value):
        raise ValueError(f"{err_prefix} must be a list[str]. Got: {value!r}")
    return value

def validate_definitions(defs: Dict[str, List]) -> None:
    for name, entry in defs.items():
        if not isinstance(entry, list) or len(entry) != 4:
            raise ValueError(f"Module '{name}' entry must be [IsRuntime, Desc, PublicDeps, PrivateDeps]. Got: {entry!r}")
        is_runtime, desc, pub, priv = entry
        if not isinstance(is_runtime, bool):
            raise ValueError(f"Module '{name}' IsRuntime must be bool. Got: {is_runtime!r}")
        if not isinstance(desc, str):
            raise ValueError(f"Module '{name}' Description must be str. Got: {desc!r}")
        _ensure_list_strings(pub,  f"Module '{name}' PublicDependencies")
        _ensure_list_strings(priv, f"Module '{name}' PrivateDependencies")

def load_uplugin(uplugin_path: Path) -> dict:
    with uplugin_path.open("r", encoding="utf-8") as f:
        return json.load(f)

def save_uplugin(uplugin_path: Path, data: dict) -> None:
    # keep it readable and stable in git
    with uplugin_path.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=4)
        f.write("\n")

def upsert_uplugin_modules(uplugin: dict, module_entries: List[dict]) -> None:
    existing = uplugin.get("Modules", [])
    if not isinstance(existing, list):
        raise ValueError("uplugin['Modules'] must be a list.")

    by_name = {}
    for m in existing:
        if isinstance(m, dict) and "Name" in m:
            by_name[m["Name"]] = m

    # Upsert
    for new_m in module_entries:
        name = new_m["Name"]
        if name in by_name:
            by_name[name].update(new_m)
        else:
            existing.append(new_m)

    # optional: stable sort by name (nice in git)
    existing.sort(key=lambda x: x.get("Name", ""))
    uplugin["Modules"] = existing

def write_module_files(source_root: Path, module_name: str, public_deps: List[str], private_deps: List[str]) -> None:
    print(f"Writing Module: {source_root}, ModuleName: {module_name}")
    module_root = source_root / module_name
    public_dir = module_root / "Public"
    private_dir = module_root / "Private"

    public_dir.mkdir(parents=True, exist_ok=True)
    private_dir.mkdir(parents=True, exist_ok=True)

    build_cs_path = module_root / f"{module_name}.Build.cs"
    header_path = public_dir / f"{module_name}.h"
    cpp_path = private_dir / f"{module_name}.cpp"

    build_cs_text = BUILD_CS_TEMPLATE.format(
        ModuleName=module_name,
        PublicDeps=_format_cs_string_array(public_deps),
        PrivateDeps=_format_cs_string_array(private_deps),
    )
    header_text = MODULE_HEADER_TEMPLATE.format(ModuleName=module_name)
    cpp_text = MODULE_CPP_TEMPLATE.format(ModuleName=module_name)

    build_cs_path.write_text(build_cs_text, encoding="utf-8", newline="\n")
    header_path.write_text(header_text, encoding="utf-8", newline="\n")
    cpp_path.write_text(cpp_text, encoding="utf-8", newline="\n")

def main() -> None:
    validate_definitions(OUTPUT_MODULE_DEFINITIONS)

    uplugin_path = Path(TARGET_ABSOLUTE_PATH_UPLUGIN_FILE)
    source_root = Path(TARGET_ABSOLUTE_PATH_SOURCE)

    if not uplugin_path.exists():
        raise FileNotFoundError(f"uplugin not found: {uplugin_path}")
    if not source_root.exists():
        raise FileNotFoundError(f"Source folder not found: {source_root}")

    uplugin = load_uplugin(uplugin_path)

    module_entries_for_uplugin: List[dict] = []

    for module_name, (is_runtime, desc, pub_deps, priv_deps) in OUTPUT_MODULE_DEFINITIONS.items():
        # Merge defaults + module-specific
        merged_public = _dedupe_preserve_order(DEFAULT_PUBLIC_DEPENDENCIES + list(pub_deps))
        merged_private = _dedupe_preserve_order(DEFAULT_PRIVATE_DEPENDENCIES + list(priv_deps))

        # Write disk files
        write_module_files(source_root, module_name, merged_public, merged_private)

        # Prepare uplugin module entry
        m_entry = {
            "Name": module_name,
            "Type": _module_type(module_name, is_runtime),
            "LoadingPhase": "Default",
        }
        if desc:
            m_entry["Description"] = desc

        module_entries_for_uplugin.append(m_entry)

    upsert_uplugin_modules(uplugin, module_entries_for_uplugin)
    save_uplugin(uplugin_path, uplugin)

    print(f"Done. Wrote {len(OUTPUT_MODULE_DEFINITIONS)} modules + updated: {uplugin_path}")

if __name__ == "__main__":
    main()
