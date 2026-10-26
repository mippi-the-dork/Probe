# Probe 0.15.3

Probe is an **Unreal Engine 5.8.3 Editor-only QA plugin** for creating, executing, and organizing QA checks against plugins, Editor UI registrations, assets, actor state, reflection, and properties. Probe also supports safe temporary UObject fixtures, reversible Editor selection actions, and one-click plugin QA Pack generation. This source package targets Windows 64-bit; no engine source modifications.


## 0.15.3: Native Level Editor Details search integration

Adds the interactive `Probe.Driver.EditorDetailsSearch` Automation Spec. This test uses Unreal's Automation Driver to **click and type into the native Level Editor Details 1 search field**, verifies that the actual Details view has applied the filter, then restores the original search text, keyboard focus and selection. It does not modify the current map, actor properties or Content assets. Open the Details 1 tab before testing. Simulated input runs on a worker thread; validation and cleanup run on the Editor thread.

The independent release gate now optionally evaluates **both** UI specs through `--ui-log` and `--require-ui`. The regular unattended gate remains headless and does not claim UI coverage. The 15-case Smoke v2 baseline, all 206 existing step IDs and all 154 built-in templates remain unchanged. See [0.15.3 usage](Docs/SELF_TEST_0153.md) and [0.15.3 QA checklist](Docs/QA_CHECKLIST_0153.md).

**Validation:** the previous 0.15.2 Smoke and SlateButton tests passed in UE 5.8.3. This new Details UI spec still requires an engine compile and runtime validation; Python gate self-checks and source consistency checks pass. CQTest remains under evaluation and is not a production dependency.


## 0.15.2: Slate Automation Driver proof and release gate

Adds the native `Probe.Driver.SlateButton` Automation Spec, which drives a temporary Slate button by stable automation ID, with input executed on a worker thread and Editor-thread cleanup. It is a **development prototype**, not yet a general-purpose UI step. Run `Automation RunTests Probe.Driver.SlateButton` in a visible UE 5.8.3 Editor after compiling.

Adds `Tools/RunProbeReleaseGate.py` to independently verify Smoke v2 JSON and four required native automation outcomes. Use `--run --editor <UnrealEditor.exe> --project <Project.uproject>` for sequential fresh Editor sessions or `--report ... --native-log ...` for offline verification. The existing 15-case Smoke gate and 206 step types have not changed. See [0.15.2 instructions](Docs/SELF_TEST_0152.md) and [validation checklist](Docs/QA_CHECKLIST_0152.md).

**Validation:** source checks and independent report/log checks succeeded, including the user's real 0.15.1 results. Engine compilation, the new Slate input test, and fresh unattended release-gate launching have not yet been verified.

## 0.15.1: Expanded Self-Test Smoke and failure isolation

Open **Tools > Debug > Output Log** and run `Probe.SelfTest`. This executes **15 transient tests** against Probe's real runner, including deliberately Failed, Error and Skipped outcomes. The expected gate is **15/15**, **PASS=6 FAIL=4 ERROR=3 SKIP=2**, with zero mismatches. The test does not create Content assets, change the level or simulate user input. It exports a structured JSON report under `<Project>/Saved/Probe/SelfTests` with **per-step status, index, assertion ID, and diagnostics**.

New coverage includes plugin installation, reflected property lookup, fixture inequality, an intentional failure after mutating a fixture, stop-on-first-failure behavior, and detection of an unavailable extension provider. An additional native Unreal Automation Framework regression (`Probe.SelfTest.FailureIsolation`) checks cross-run isolation, deterministic repeat behavior, and early exit. The previous two native tests and all 206 built-in step IDs remain intact.

An updated independent Python verifier checks the report's per-step evidence and detects corrupt results. It can also validate reports produced by Probe 0.15.0 (`Smoke.v1`). See `Docs/SELF_TEST_0151.md` and `Docs/QA_CHECKLIST_0151.md` for the exact test commands and expected output. **Unreal 5.8.3 compilation and execution of this update remain to be validated in the user's Editor.**

### Work still required for Probe 0.15

The Automation Framework and a disposable Automation Driver Slate click have been validated in UE 5.8.3. Native Details search UI simulation in 0.15.3 and CQTest fixture comparison still require validation. The observed MetaSound warnings occur during Unreal's global Automation Framework test discovery, not during `Probe.SelfTest`; they are not suppressed by Probe. See `Docs/ARCHITECTURE_AUDIT_015.md`.


## Development utility: generate every template as a Probe Test

For rapid template QA in the Unreal Editor, open **Tools > Debug > Output Log** and enter:

```text
Probe.GenerateTemplateTests
```

Probe creates one asset for each template currently registered (including extension-provided templates) beneath **`Content/Probe/`**, which Unreal displays as **`/Game/Probe/`**. Category and subgroup folders follow the same classification used by the Templates menu. For example, a module template appears under `/Game/Probe/Plugins_Modules/Modules/`.

The command attempts to save all newly generated Test Assets and logs counts for **created**, **skipped**, and **failed** items. It is safe to rerun: existing generated assets are left intact, and only missing template assets are created. No existing Test Assets elsewhere are modified or deleted. Generated names contain a stable template ID hash to avoid collisions.

**Important:** Most templates intentionally have **Needs Configuration** enabled. The generator creates *examples*, not a passing QA suite. Review their targets, input fields, and safety permissions before marking them Ready or using Run All. To remove the generated examples later, delete the `/Game/Probe/` folder in the Content Browser after checking it contains only disposable generated assets. This developer command is temporary and is not part of the normal authoring UI.


## Install

Copy `Probe/` into `<Project>/Plugins/`, generate project files if needed, build the Editor target, then open **Tools > Testing > Probe**. Double-click any Probe Test asset to open it in Probe; double-click a Probe Pack asset to open **Probe Packs**.

## What's new in 0.14.1

- Added the temporary console command **`Probe.GenerateTemplateTests`**. It creates one Test Asset for every currently registered template beneath **`/Game/Probe/<Category>/<Group>`**, using the same category and subgroup classification as Probe's Templates menus.
- Includes standard templates, Blank Test, Probe Smoke Test, and extension-provided templates from loaded plugins.
- Uses stable asset paths so the command is safe to rerun. Existing generated assets are skipped rather than overwritten; new assets are saved and results are reported in the Output Log.
- Existing tests, Packs, step types, and serialized assets remain compatible. No existing test assets are altered by the generator.

## What's new in 0.14.0

- **Duplicate Step crash fix:** A copied step is now stored in a local value before insertion into the `TArray`. Passing an existing array element to `Insert` could reallocate the array while it was still reading the element, invalidating the source. Duplication, deletion, and reordering also preserve expanded-row state more consistently. Existing assets and their assertion IDs are unchanged.
- **16 new test-owned actor actions and assertions**, bringing the built-in catalog to **195 step types**. Move/rotate/scale a transient actor, compare its transform, attach and detach test-owned actors, verify parent relationships, count direct test-owned children, check proximity, and assert negative tag/folder/visibility/selection states.
- **12 new Test Actions templates**, bringing the catalog to **143 templates**: location, rotation, scale, combined transform, parent/child attachment, detachment, reparenting, distance, tag removal, hide/show, folder exclusion, and selection cleared.
- **Authoring UX:** Spatial values accept **X,Y,Z** or **Pitch,Yaw,Roll** as comma-separated numbers. Maximum Distance uses centimeters; hierarchy uses local Actor IDs. Incorrect triples are identified before a template can be marked Ready. All new templates require review and explicit **Allow Transient Editor Actors** permission.
- **Automation regressions:** `Probe.Editor.StepDuplicationSafety` covers repeated duplication including extension IDs and large string fields. `Probe.Runner.TransientActorTransformsAndHierarchy` checks transform, parent/child changes, and cleanup after both a successful and failing run.

### Important limits

These operations affect **only actors spawned within the same Probe Test run**. They cannot inspect or control Focus/Latch-specific Solo or Lock features without dedicated provider integrations, and they cannot click actual Outliner controls yet. Spawning actors in an Editor world can dirty the map or leave an empty folder after cleanup. Start in a disposable map, save before running, and do not save the test map afterward unless you deliberately want the changes.

## Previous capabilities (0.12.0)

- **Controlled temporary Editor actors (15 new step types, seven starter templates):** With an explicit per-Test **Allow Transient Editor Actors** option, Probe can spawn a transient actor, set its label/folder/tags, select or hide/show it, assert its state, and destroy it on completion or failure. Actor IDs and permissions are scoped to a single test. These operations act only on actors Probe created.
- **Safety first:** The permission defaults to **off** for old and new Test Assets; the seven templates begin **Needs Configuration**. Operations are blocked during PIE and if the Editor world changes. The active level may become dirty or retain an empty folder. Start in a disposable map and save your working level first.
- **Behavioral QA starter guide:** `Docs/START_BEHAVIOR_QA.md` explains the exact first-run process, supported fields, test packs, failure cleanup, and limits of Editor UI automation.

- **179 built-in step types** across **nine categories**, plus **131 templates**. The five new Editor selection actions can clear, select actors by label or tag, select an available actor, and restore the original selection. Probe restores it after **every test**, even if an assertion fails. No saved level actors or assets are modified by these actions.
- **Plugin QA Pack generator:** Choose an enabled plugin by friendly name in Probe Packs, and automatically create four ready-to-run baseline Test assets plus one Pack. Descriptor names, versions, module declarations, and currently loaded modules are captured automatically. This is the first no-code path to routine QA for Focus, Latch, and other Editor plugins.
- **Four new selection templates:** Selection Clear And Restore (ready immediately), Selection First Available Actor, Selection By Actor Label, and Selection By Actor Tag (configuration needed for level-dependent runs).
- **Probe Packs redesigned:** asset identity and native Slate icons based on the supplied Probe Pack SVG; a resizable bulk Add Tests dialog, selectable test rows with run ordering, search, removal, creation of new tests, and Content Browser drag and drop. A per-session Last Result column provides immediate run feedback.
- **Reference validation** in **Probe Packs**, with separate **Validate Pack** and project-wide **Validate All** buttons. This audits Pack references, repeated references within a Pack, duplicate stable Test IDs, and positive asset assertions that reference missing assets. An **Asset Does Not Exist** assertion is intentionally excluded from the missing-reference check.
- Each Pack member displays **Valid**, **Redirector**, or **Missing / Invalid**. **Replace...** picks an existing Probe Test to substitute in that slot; the trash action only removes the Pack entry. Both require **Save Pack** to persist and support Undo/Redo.
- Running a Pack checks its references against the Asset Registry before attempting loads. Missing paths now report a test error without calling the package loader, reducing stale-reference Output Log warnings from deleted tests.
- **Temporary fixtures** can be created, mutated, compared, and released. Probe automatically releases remaining fixtures at the end of every run, including if an assertion fails. Fixture IDs are scoped to a single test run.
- Existing Probe Test Assets, Packs, assertion IDs, reports, icons, and navigation remain compatible; no migration is necessary.

## Reference validation and recovery

1. Open **Probe > Manage Packs** (or double-click a Probe Pack).
2. Click **Validate All** to inspect all Packs, or **Validate Pack** to inspect only the selected Pack. The report lists Packs checked, issues, duplicate Test IDs, and any missing positive asset assertion targets.
3. For a missing or moved Pack member, choose **Replace...** and search for its new location. If it was deliberately deleted, remove that Pack entry.
4. Save the Pack, validate again, and run it.
5. If duplicate stable Test IDs are reported, inspect the named assets. Resolve by deliberately assigning a new unique ID in a future dedicated repair workflow or recreating the duplicated test through Probe's duplication action. Reference validation itself never silently changes test IDs.

This validation is read-only until you explicitly replace or remove a member. It does not run fix-up redirectors on the project or attempt to load nonexistent test packages. The project-wide audit may load **existing** Test/Pack assets to read their IDs and contents.

## Transient fixture action workflow

The **Test Actions** category includes:

| Step | Inputs | Purpose |
|---|---|---|
| Create Temporary Object | Object Class; New Fixture ID | Make an isolated transient non-Actor UObject |
| Set Temporary Object Property | Fixture ID; Property Name; New Value | Set bool, numeric, FString, or FName properties |
| Temporary Object Property Equals | Fixture ID; Property Name; Expected Value | Check the result of a preceding action |
| Temporary Object Property Does Not Equal | Fixture ID; Property Name; Expected Value | Check a negative expectation |
| Temporary Object Exists | Fixture ID | Check the fixture is still available |
| Destroy Temporary Object | Fixture ID | Release the fixture explicitly |

Start with **Templates > Test Actions > Temporary Boolean Mutation**, which creates a transient Probe Test object, sets its `bEnabled` value to `False`, checks that value, and destroys the fixture. The **Temporary Object Lifecycle** and **Temporary String Mutation** templates are also runnable immediately without configuration.

Fixtures are **not** actors and do not appear in the World Outliner. These first actions establish isolated setup / act / assert / cleanup for UObject behavior. Automated Editor clicks, Outliner button operations, and transaction-driven world changes are **not yet supported** and are planned for a later stage.

## A test workflow

1. Choose **New Test** or **Templates > category > starter**.
2. Choose an assertion using **Add Step > category**. Browse for classes, plugins, actors, modules and tabs where possible, and use **Use Selected Asset** for Content Browser assets.
3. Configure expected values and conditions. Invalid or empty required fields are marked within the step.
4. For template-derived tests, **uncheck Needs Configuration** when the test is ready for automated execution; it does not guarantee that the expected assertion will pass.
5. **Save**, then use **Run Selected**, **Run All**, or **Run Pack**. Use Results Explorer / Log for diagnostics and JSON Export for reports.

### Why templates may fail after configuration

Templates are examples, **not automatic fixtures**. Assertions about selected actors, exact asset counts, actor tags, redirectors, PIE, and plugin versions must be tailored to your project and current Editor state. A real Failed result means Probe executed the assertion and found a mismatch. An Error means Probe couldn't evaluate it; a Skipped result means the test was disabled or still needs configuration.

To triage multiple old example assets, multi-select them in the Content Browser and choose **Probe > Mark Needs Configuration**, then save. This explicitly holds them out of automated runs while you review their setup.

Existing 0.7.0 and older tests are intentionally not silently rewritten. If you previously created an example test with an invalid expectation, edit that asset (or mark it Needs Configuration while you work on it). For example, 0.7.0's Plugin Version template expected `0.6.0`, and its Tool Menu Section starter used the **visible label** `Testing` instead of the **internal section ID** `ProbeTools`.

## Probe Packs: from zero to runnable

1. In Probe, choose **Manage Packs**, or double-click an existing **Probe Pack** asset in the Content Browser.
2. Select **New Pack**, choose the destination folder and name, and edit its Description in the Pack header.
3. Click **+ Add Tests...** to open the large searchable, resizable multi-select dialog. Check as many existing Probe Test assets as you want and click **Add Selected**. Already-added assets do not appear again.
4. Alternatively, drag Probe Test assets from the Content Browser onto the Pack workspace. Use **New Test** in the Pack workspace to create a new Probe Test asset next to your Pack and add it automatically.
5. Each row displays the Test name, Content Browser path, readiness, reference health, and last-run result. Click the Test name to edit it in Probe. Use the arrow buttons to change run order. Search to filter the current Pack list.
6. Check multiple rows to use **Run Selected** or **Remove Selected**. Removing entries never deletes the source Test Assets.
7. **Save** (or Ctrl+S) the Pack. Use **Run Pack** for its full test list in order, or **Run Selected** for checked rows. Both results appear in Probe's Results Explorer.
8. Use **Validate Pack** for local diagnostics and **Validate All** for project-wide reference issues and duplicate stable test IDs. Expand **Reference Diagnostics** to read or copy the report. Missing references may be replaced in place.

The Pack asset type is shown publicly as **Probe Pack** with its dedicated icon. The underlying `UProbeTestPack` class remains unchanged to preserve serialized content and references. The Last Result column is session-only and clears when the Editor restarts; it is not saved to asset data.

### Existing read-only contract coverage from 0.10.0

| Category | New assertions |
|---|---|
| Plugins & Modules | Plugin Version Contains; Plugin Friendly Name Contains; Plugin Has No Content; Loaded Module Count Exactly |
| Assets | Asset Count Direct At Least/Exactly; Asset Package Path Equals; Asset Class Name Equals |
| Classes & Reflection | Property Is Array, Object Reference, Struct, Enum, Name, String or Text; Function Is Const, Public or Deprecated; Class Has Metadata |
| Actors & World | Actor Label Count At Most; Actor Count With Tag At Most; Actor Folder Count Exactly/At Most |
| Selection | Selected Actor Is Temporarily Hidden; Selected Actor Is Not Temporarily Hidden |

The 25 matching templates appear in categorized menus and start in **Needs Configuration**. Set meaningful values for the project and clear that state only when ready to execute. The direct-folder asset count checks are non-recursive; earlier folder count checks remain recursive.

## Developer and distribution notes

Read [Docs/EXTENDING_PROBE.md](Docs/EXTENDING_PROBE.md) for test provider modules, assertion and template registration, and optional companion plugin packaging. `Tools/ValidateProbeCatalog.py` is a source-only developer check; normal users do not need to run it. Probe now supports **temporary UObject test actions and reversible Editor selection actions**. It now supports explicit opt-in transient actor creation in the active Editor world, but does not yet perform Editor UI clicks or Undo/Redo sequences. The actor actions require a disposable test map for initial QA.

See [Docs/ASSERTION_UX.md](Docs/ASSERTION_UX.md) for assertion authoring behavior and limitations, and [Docs/QA_CHECKLIST.md](Docs/QA_CHECKLIST.md) for acceptance cases. This package has **not** been compiled or run in a local UE 5.8.3 engine environment; it requires a local build and QA pass.

## Start Testing Your Editor Plugins

Open **Probe Packs**, select **Plugin QA Pack...**, search for an enabled plugin such as Focus, and choose it. Probe creates four **ready-to-run** Probe Test assets and a Probe Pack in `/Game/ProbeTests/PluginQA/<PluginName>/`:

- **Availability:** Plugin Is Installed and Plugin Is Enabled.
- **Descriptor:** Friendly Name, Version Name, and Category against the currently installed descriptor. These are pinned expectations; deliberately changing the plugin version requires updating the test.
- **Modules:** Exact declared module count, each declared module, and a loaded check for modules loaded at generation time.
- **Editor Preflight:** Editor world is loaded and PIE is not running.

The resulting Pack is selected in the Packs workspace. Click **Run Pack**, inspect individual results in Probe, then export the JSON report if desired. Re-running this Pack after plugin updates detects changes to these contracts. Generating again creates a **new** Pack and Test assets rather than overwriting prior QA assets.

### Selection Actions

Test Actions also supports clearing Editor actor selection, selecting the first available actor, selecting by exact Outliner label, selecting by Actor Tag, and explicitly restoring the original selection. The runner automatically restores selection after **each test**, including on failed or erroneous steps. These actions do **not** spawn actors, modify properties, or edit levels; they only manipulate the Editor selection temporarily. The ready-to-run **Selection Clear And Restore** template is a good first validation. Additional selection templates require an appropriate level or configured Actor Label / Tag.

See `Docs/START_PLUGIN_QA.md` for baseline plugin checks, `Docs/START_BEHAVIOR_QA.md` for actor workflow setup and safety boundaries, and `Docs/QA_CHECKLIST_012.md` for the version-specific acceptance tests. Full native UI click automation (Solo, Outliner hierarchy locks, Blueprint editor interactions) is **not** yet implemented; those will need plugin-specific action adapters or future Probe actions.


## Test readiness and world authorization (0.14.0)

Probe displays **Test Readiness** as a status section with **Mark Ready / Mark Needs Review** actions. A test with incomplete inputs cannot be marked Ready. Actor fixture steps display a separate **World Actions** authorization section, with explicit authorize/revoke actions. Actor fixtures are still restricted to actors created by the test and may dirty the current level; use a disposable map.

Add Step, Change Assertion, and Templates now use a shared category/subcategory hierarchy so the menus remain navigable as the library grows. This version includes 206 built-in step types and 154 templates, including eleven more test-owned actor checks and matching workflow templates. Existing serialized enum IDs remain unchanged.

The `Probe.Editor.StepDuplicationSafety` Automation Framework test can be run via Unreal's Output Log command input using `Automation RunTests Probe.Editor.StepDuplicationSafety`.

See `Docs/QA_CHECKLIST_014.md` for first-run checks. Plugin-specific Focus automation will be based on the current Focus source rather than the outdated public repository version.
