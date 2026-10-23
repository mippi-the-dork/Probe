# Probe 0.10.0

Probe is an **Unreal Engine 5.8.3 Editor-only QA plugin** for creating, executing, and organizing QA checks against plugins, Editor UI registrations, assets, actor state, reflection, and properties. Probe also supports its first safe action steps on temporary UObject fixtures. This source package targets Windows 64-bit; no engine source modifications.

## Install

Copy `Probe/` into `<Project>/Plugins/`, generate project files if needed, build the Editor target, then open **Tools > Testing > Probe**. Double-click any Probe Test asset to open it in Probe; double-click a Probe Pack asset to open **Probe Packs**.

## What's new in 0.10.0

- **159 built-in step types** across **nine categories**, with **25 more read-only assertions** for plugin descriptors, reflection, asset registry, actor labels/folders and selection. Probe includes **120 templates**, including 25 new configurable contract starters. Temporary UObject fixture actions from 0.9 remain available. Test Actions are synchronous and operate only on temporary objects; they do not create actors or manipulate loaded levels.
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

Fixtures are **not** actors and do not appear in the World Outliner. These first actions establish isolated setup / act / assert / cleanup for UObject behavior. Automated editor clicks, Outliner interactions, and transaction-driven world changes are **not yet supported** and are planned for a later stage.

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

### New assertion coverage in 0.10.0

| Category | New assertions |
|---|---|
| Plugins & Modules | Plugin Version Contains; Plugin Friendly Name Contains; Plugin Has No Content; Loaded Module Count Exactly |
| Assets | Asset Count Direct At Least/Exactly; Asset Package Path Equals; Asset Class Name Equals |
| Classes & Reflection | Property Is Array, Object Reference, Struct, Enum, Name, String or Text; Function Is Const, Public or Deprecated; Class Has Metadata |
| Actors & World | Actor Label Count At Most; Actor Count With Tag At Most; Actor Folder Count Exactly/At Most |
| Selection | Selected Actor Is Temporarily Hidden; Selected Actor Is Not Temporarily Hidden |

The 25 matching templates appear in categorized menus and start in **Needs Configuration**. Set meaningful values for the project and clear that state only when ready to execute. The direct-folder asset count checks are non-recursive; earlier folder count checks remain recursive.

## Developer and distribution notes

Read [Docs/EXTENDING_PROBE.md](Docs/EXTENDING_PROBE.md) for test provider modules, assertion and template registration, and optional companion plugin packaging. `Tools/ValidateProbeCatalog.py` is a source-only developer check; normal users do not need to run it. Probe now supports **limited transient UObject test actions**. It does not yet perform Editor clicks, actor creation, or Undo/Redo sequences; those will require an Editor interaction layer and more extensive isolation.

See [Docs/ASSERTION_UX.md](Docs/ASSERTION_UX.md) for assertion authoring behavior and limitations, and [Docs/QA_CHECKLIST.md](Docs/QA_CHECKLIST.md) for acceptance cases. This package has **not** been compiled or run in a local UE 5.8.3 engine environment; it requires a local build and QA pass.
