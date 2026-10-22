# Probe 0.5.0

Probe is a Windows-focused, Unreal Engine 5.8.3 **Editor-only** QA test authoring and execution plugin. It provides reusable **Probe Test** Data Assets with a native-style dockable Editor, contextual Content Browser commands, read-only Editor assertions, structured results, a selectable/copyable log, and JSON export.

## Compatibility & installation

- Unreal Engine **5.8.3**, Editor only, source distribution. No Engine source modifications.
- Close the Editor, replace `<Project>/Plugins/Probe` with the supplied `Probe` folder, and build your project Editor target.
- Open **Tools > Testing > Probe**, or double-click any Probe Test in the Content Browser.
- Existing assets retain their `UProbeTestAsset` type, test IDs, fields, and original 11 enum values. New enum entries are appended to keep the original numeric assignments stable.

## New in 0.5.0

- **30 new read-only assertions**, now **41 total**. They cover the kind of data often exposed by Editor plugins: plugin/module health, registered ToolMenus, open tabs, console variables, asset lifecycle and registry tags, class interfaces and reflected functions, actor hierarchy/components, selection, and object values.
- One shared `ProbeAssertionCatalog.h` supplies the display names, descriptions, expected inputs, and **eight categories**, used by **Add Step** and **Change Assertion**. Templates are categorized the same way.
- **11 additional test templates**, plus the original ten configured templates and Blank. Templates create normal editable Probe Test Data Assets, not hard-coded scripts.
- Conditional input fields. For example, Editor Tab Open asks for a tab ID, Asset Registry Tag Equals asks for the asset path, tag name, and expected value; Actor Has Component asks for a World Outliner label and component class.
- Internal Unreal Automation tests: `Probe.Catalog.AllAssertionsRegistered` and `Probe.Runner.EditorPluginAssertions`.

## Authoring workflow

1. Open Probe. Use **New Test** or the categorized **Templates** menu.
2. Choose **Add Step > Category > Assertion**, or use the assertion picker on an existing step.
3. Set only the fields that appear for that assertion. Hover over menu entries for descriptions; fill in any required paths, names, counts, or comparison values.
4. Save Test Asset. **Run Selected**, **Run All**, or run tests from the Content Browser right-click menu.
5. Review Results Explorer/diagnostics, Copy Results/Copy Diagnostic, or Export JSON to `<Project>/Saved/Probe/Reports/`.

**Assertion outcomes:** Incorrect or unmet actual state returns Failed. Missing required configuration or unsupported value types return Error. Disabled Test Assets return Skipped. Probe stops on the first non-passing step per test, so later steps do not execute.

## Assertion reference (41)

Most paths require an exact Unreal object or class path, e.g. `/Game/ProbeTests/Probe_Smoke.Probe_Smoke` or `/Script/Engine.Actor`. For actor tests, `Target` usually refers to the **World Outliner actor label**, not the UObject path. Actor-label matching is case-sensitive.

### Plugins & Modules

| Assertion | What it checks |
|---|---|
| Plugin Is Enabled | Confirm a named plugin is enabled. |
| Plugin Is Installed | Check that a plugin is discoverable, regardless of enabled state. |
| Plugin Is Disabled | Check that an installed plugin is explicitly disabled. |
| Module Is Loaded | Check whether an Unreal module is loaded. |

### Editor UI

| Assertion | What it checks |
|---|---|
| Editor Tab Is Open | Check whether a tab with this internal tab ID is open. |
| Tool Menu Is Registered | Check whether a named ToolMenus menu is registered. |

### Editor State

| Assertion | What it checks |
|---|---|
| Editor World Exists | Check that the level Editor world is available. |
| Console Variable Exists | Check an engine console variable is registered. |
| Console Variable Equals | Compare a console variable with its current string value. |

### Assets

| Assertion | What it checks |
|---|---|
| Asset Exists | Find an asset by its object path. |
| Asset Is Class | Verify an asset is an instance of a class. |
| Asset Count In Folder At Least | Count assets under a /Game or plugin content folder, recursively. |
| Asset Has Unsaved Changes | Check an asset package is dirty (loads asset if necessary). |
| Asset Has No Unsaved Changes | Check an asset package is clean (loads asset if necessary). |
| Asset Is Loaded | Check whether an asset object is already in memory without loading it. |
| Asset Registry Tag Equals | Read a searchable asset registry tag without loading the asset. |

### Classes & Reflection

| Assertion | What it checks |
|---|---|
| Class Exists | Resolve a class by Unreal class path or name. |
| Class Derives From Class | Verify inheritance between two classes. |
| Class Implements Interface | Check a class implements a UInterface. |
| Class Has Property | Check a reflected property exists on a class. |
| Class Has Function | Check a reflected UFUNCTION exists on a class. |

### Actors & World

| Assertion | What it checks |
|---|---|
| Actor Count At Least | Count level actors (at least). |
| Actor Class Count At Least | Count actors derived from a class (at least). |
| Actor With Tag Exists | Find a level actor with a tag. |
| Actor With Label Exists | Find an actor by its exact Outliner label. |
| Actor With Label Does Not Exist | Ensure there is no actor with this exact Outliner label. |
| Actor With Tag Does Not Exist | Ensure no level actor has a particular tag. |
| Actor Has Component | Find a component class on an actor identified by Outliner label. |
| Actor Folder Equals | Check the World Outliner folder path of an actor. |
| Actor Count Exactly | Require exactly this many level actors. |
| Actor Class Count Exactly | Require exactly this many actors of a class. |

### Selection

| Assertion | What it checks |
|---|---|
| Selected Actor Count At Least | Count selected level actors (at least). |
| Selected Actor Count Exactly | Require exactly this many selected level actors. |
| Selected Actor Label Equals | Require one selected actor with an exact label. |
| Selected Actor Is Class | Require one selected actor derived from a class. |
| Selected Actor Has Tag | Require one selected actor with a tag. |

### Properties & Values

| Assertion | What it checks |
|---|---|
| Object Property Equals | Compare an object property with an expected value. |
| Object Is Loaded | Check whether an object is already loaded without loading it. |
| Object Property Exists | Check a loaded or loadable object has a reflected property. |
| Object Property Does Not Equal | Compare a reflected property with a value and expect inequality. |
| Actor Property Equals | Compare an actor property by Outliner label. |

## Editor-plugin testing examples

### Probe health / startup

Create a single test with steps: **Plugin Is Installed** (`Probe`), **Plugin Is Enabled** (`Probe`), **Module Is Loaded** (`ProbeEditor`), **Editor Tab Is Open** (`ProbeWorkspace`, only after opening Probe), and **Tool Menu Is Registered** (`LevelEditor.MainMenu.Tools`). The last two make sense during an interactive Editor session, not a headless test run.

### Testing another installed Editor plugin

For Focus, Latch, Prune, Hue, or another Editor plugin, begin with **Plugin Is Installed** and **Module Is Loaded**, using the actual plugin/module identifiers. Add **Class Has Function / Property** for any APIs exposed through Unreal reflection, **Tool Menu Is Registered** for menu paths, or **Editor Tab Is Open** for tab IDs. These checks only verify accessible state; they **do not** prove a UI button functions.

### Asset lifecycle

**Asset Exists** verifies the registry path. **Asset Is Loaded** checks memory without loading. **Asset Has Unsaved Changes** / **Asset Has No Unsaved Changes** inspect the package's current dirty flag and may load the asset if needed. **Asset Registry Tag Equals** works only with tags published to the Asset Registry (not arbitrary UObject fields). **Asset Count In Folder At Least** counts assets recursively under a valid package folder, such as `/Game/ProbeTests`.

### World Outliner and actor workflow checks

**Actor With Label Exists / Does Not Exist** and **Actor Folder Equals** inspect the active level's actor hierarchy. **Actor Has Component** can verify native or plugin component classes on an actor found by label. **Selected Actor Count Exactly** checks selection state after a manual action. Use **Use Selected Actor** to capture an exact label when configuring compatible actor assertions.

### Reflected property checks

**Object Property Equals**, **Object Property Does Not Equal**, and **Actor Property Equals** support bool, numeric (with tolerance), `FString` and `FName`. `Object Property Exists` and `Class Has Property / Function` inspect Unreal reflection. Unreflected private C++ members and arbitrary Slate widget state are not accessible through these assertions.

## Limits and safety

- **Read-only assertions.** Probe does not create actors, change selected assets, press Slate buttons, move actors, execute Editor commands, replay Undo/Redo, or control drag/drop as test actions. It checks observable current Editor state. Some checks resolve or load assets/classes; they do not intentionally modify their contents.
- **No fixture lifecycle or full UI driver yet.** Selection, tab, and active-level-dependent assertions work best in interactive Editor sessions. They are not necessarily deterministic between projects or clean launches.
- **ToolMenus and tab checks:** registered menus are not proof that menu entries actually execute. `Editor Tab Is Open` checks the global tab manager's live tabs; some asset editor tabs are managed by a local tab manager and may not be visible there.
- **Input typing and values:** `MinimumActorCount` is reused as the integer threshold for asset counts, exact actor counts, and selection counts to preserve asset serialization. It appears contextually labeled in Probe's authoring UI. Any assertion that expects a class or interface needs that class to be available to the Editor.
- Probe's two self-tests use Unreal's built-in automation runner; ordinary Data Asset steps are executed by Probe's own synchronous runner. Integration with full Editor Automation Driver and standalone orchestration is future work.

## Validation status

This source package has undergone **source review, catalog coverage checks, and ZIP integrity validation**, but **has not been compiled or launched against an Unreal Engine 5.8.3 installation** in this environment. Validate it using `Docs/QA_CHECKLIST.md` before relying on it for releases.
