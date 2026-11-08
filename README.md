# Probe 0.15.4-dev20

## Dev 20: Unified Test Library in the main Probe workspace

**Verified baseline:** Dev 19 Hotfix 1 compiled and passed **127/127 Probe native tests**. Its provider and authored JSON/manual integration tests passed. Dev 20 adds **9 new native tests (136 expected)** and is **source-validated only** until a new Unreal build and manual UI verification.

- **Main workspace Test Library:** source-aware search, source filter, safe scrolling/200-row cap, persistent collapsed state, inspect and Run Selection for **authored Test Assets, authored Packs, portable Tests/Packs and native Unreal Automation tests**.
- **Authored Test/Pack:** existing asset editing, queue, Pack policies, Results Explorer and History remain unchanged. **Run All Authored** explicitly excludes native and portable tests.
- **Portable Test/Pack:** run directly from the main Probe tab; action-bearing definitions require per-run confirmation and can be cancelled. Actual portable step results feed Probe's Results Explorer and optional History, plus the existing `ProbePortableReport/1` output.
- **Native Unreal Automation:** discover test names through the engine and submit selected names to Unreal's `Automation RunTests`; **actual native results remain in Tools > Test Automation** until a controller-aware results bridge is built. Probe does not invent native PASS results.
- **Providers window:** remains the place for JSON authoring, validation, import/export, and debugging; it is no longer required for routine portable execution.
- **No saved-format changes:** current Probe assets and `Resources/Probe` JSON remain unchanged; independent plugins still have no Probe dependency.

See [Dev 20 Unified Workspace](Docs/DEV20_UNIFIED_WORKSPACE.md), [Dev 20 QA Checklist](Docs/QA_CHECKLIST_0154_DEV20.md), and [Dev 20 Design Decisions](Docs/DEV20_DESIGN_DECISIONS.md).

**Known pre-1.0 work:** import authoritative Unreal Automation results into Probe History and design mixed-source Packs before claiming fully unified reporting/scheduling.

---

# Probe 0.15.4-dev19

## Dev 19: Portable Provider Authoring, Validation & Reporting

**Verified baseline:** Dev 18 Hotfix 2 compiled in UE 5.8.3 and passed **115/115 Probe native tests**. Its separate `ProbeExampleProvider` passed fixture lifecycle (3 steps) and both Tests in the authorized Actions Pack. Dev 19 adds **12 new native tests (127 expected)** and requires fresh UE compilation and manual verification.

- **Provider authoring:** Generate validated `Resources/Probe` starter folders (manifest, read-only assertion alias, Template, Test, Pack) under the project Saved directory; no external plugin is automatically modified or made dependent on Probe.
- **Provider Browser:** Search; per-plugin Validate; Preview Tests/Packs with step source, authorizations and Pack order; edit JSON via the associated local editor; copy unchanged source definitions to Saved/Probe/DefinitionExports.
- **Portable Test import/export:** Import read-only Tests as disabled Needs Review Test Assets under `/Game/Probe/Imported`. Export read-only built-in Test Assets to a new, self-contained JSON provider draft, validated using the same UE loader. Unsafe or ambiguous exports are rejected, not silently rewritten. Privileged action Tests remain portable-only.
- **Developer console:** `Probe.Portable.Validate`, `Probe.Portable.CreateStarter`, `Probe.Portable.ImportTest`, and `Probe.Portable.ExportAsset`.
- **Machine-readable portable reports:** Versioned `ProbePortableReport/1` JSON saved under `Saved/Probe/PortableReports` after explicit runs, including Test/Pack outcomes and step evidence. Presentation distinguishes deliberate cancellation from ordinary failure, preserving raw engine status.
- **Native automation:** New tests for starter contracts, validation/round-trip, privilege isolation, reporting, persistence, and definition exports. No change to Test/Pack asset serialization, permission defaults, third-party plugin dependencies, or existing assertions/templates.

Docs: [Provider Authoring](Docs/PROVIDER_AUTHORING_DEV19.md), [Portable Reports](Docs/PORTABLE_REPORTS_DEV19.md), [Dev 19 QA Checklist](Docs/QA_CHECKLIST_0154_DEV19.md), [Design Decisions](Docs/DEV19_DESIGN_DECISIONS.md), [Provider v1 contract](Docs/PORTABLE_PROVIDER_SPEC_1.md) and [Dev 18 Actions](Docs/PORTABLE_ACTIONS_DEV18.md).

**Current status:** Source package only. The UE 5.8.3 build and Dev 19 interaction/regression tests are pending.

---

# Probe 0.15.4-dev18-hotfix1

## Dev 18: Authorized Portable Actions, Async Providers & Cleanup

**Verified baseline:** Dev 17 built in UE 5.8.3, passed **103/103 native Probe tests**, and successfully discovered/executed the independent `ProbeExampleProvider` Test and Pack. Dev 18 adds **12 native tests** for **115 expected Probe tests**; the Dev 18 package remains source-validated only until an Unreal build and manual action execution pass.

- **Optional action definitions:** A providing plugin can ship `Resources/Probe/Actions/*.json`, referenced by the existing `ProbeProvider/1` manifest. It still does not include or link Probe. Ordinary Dev 17 JSON providers remain compatible.
- **Explicit run authorization:** Read-only `RunTest`/`RunPack` commands reject action-bearing definitions. New `RunTestActions`/`RunPackActions` commands and a confirmation in the Provider Browser authorize the requested run only. Saved Test Assets cannot acquire these transient permissions.
- **Test-owned fixtures:** JSON `native-fixture` actions map only to a narrow allow-list of existing Probe managed UObject/Actor fixture operations. Actor fixtures may mark an Editor level dirty; no arbitrary live input or global-selection actions are exposed.
- **Provider-specific native actions:** `reflected-action` requires a provider-owned native static UFUNCTION with `ProbeActionStart` metadata and a separate mandatory `ProbeActionCleanup` hook, validated before invocation. The plugin's own code must restrict side effects to reversible/test-owned state.
- **Asynchronous providers:** `reflected-async` adds required Poll and Cancel hooks, 100 ms game-thread polling, bounded timeouts and cooperative `Probe.Portable.Cancel` requests. Cleanup is attempted even after failure, timeout, and cancellation; cleanup errors fail the test.
- **Provider Browser/diagnostics:** Lists action counts, makes action runs distinct, supports cancellation and prints summary/diagnostic details on Refresh. No test executes during discovery.
- **Independent sample:** `Examples/ProbeExampleProvider` includes JSON-only fixture actions and a native async sample that compiles with no Probe module dependency.

**Documentation:** [Portable Actions Contract](Docs/PORTABLE_ACTIONS_DEV18.md), [Dev 18 QA Checklist](Docs/QA_CHECKLIST_0154_DEV18.md), [Dev 18 Design Decisions](Docs/DEV18_DESIGN_DECISIONS.md). This is still a **provisional pre-1.0 integration API**, not a native C++ security sandbox or release compatibility guarantee. Existing independent native Automation Framework tests remain required for other plugins.

---

# Probe 0.15.4-dev17

## Dev 17: Dependency-free JSON Provider API

**Verified baseline:** Dev 16 built and passed **93/93 native Probe Automation Framework tests**, and manual checks of Groups, Recipes, workspace and Results succeeded. Dev 17 adds **10 native tests** for **103 expected Probe tests**. It remains **source-validated only** until a UE 5.8.3 build and Editor/manual checks pass.

- **Optional `Resources/Probe` folder:** Enabled plugins can provide a versioned `manifest.json` and declarative assertion, template, Test and Pack JSON files, without linking to or requiring Probe. When Probe is absent, these files are inert.
- **Automatic discovery with diagnostics:** Probe scans enabled plugins when its editor module starts and on explicit Refresh. Unsupported schema versions, unsafe file paths, missing definitions, duplicate IDs and cross-provider references are rejected before registration. Discovery does not execute tests.
- **Portable execution:** JSON Tests and Packs run through the existing Probe runner as transient in-memory Test Assets. No `.uasset` is generated, and portable Tests are not automatically mixed into Run All.
- **Provider assertions:** Built-in assertion aliases and optional explicitly marked `ProbeReadOnly` reflected native checks, limited to an exact static signature and owning plugin module. This is not an arbitrary function invocation API.
- **Unreal Editor interface:** **Tools > Testing > Probe Providers** lists providers, validation issues, Tests and Packs with explicit **Run** controls. Console equivalents: `Probe.Portable.Refresh`, `Probe.Portable.RunTest <ID>`, `Probe.Portable.RunPack <ID>`.
- **Independent QA:** Native tests for JSON validation, path sandboxing, provider ownership, forbidden input, missing references, malformed JSON, transient execution, duplicate definitions, and inert reflection declarations.

**Safety/scope:** Provider v1 is **read-only**. Mutating actions, provider-owned async operations, fixtures and permissions need the separate Dev 18 contract. Built-in `UProbeTestAsset`/`UProbeTestPack` formats and enum ordinal values remain unchanged, and no other plugins have Probe dependencies or test migrations. No UE compilation was performed here.

Docs: [Portable Provider Specification](Docs/PORTABLE_PROVIDER_SPEC_1.md), [Dev 17 QA Checklist](Docs/QA_CHECKLIST_0154_DEV17.md), [Design Decisions](Docs/DEV17_DESIGN_DECISIONS.md), [Standalone Example Provider](Examples/ProbeExampleProvider/).

---

# Probe 0.15.4-dev16

## Dev 16: Workspace & Authoring UX Overhaul

**Verified baseline:** Dev 15 Hotfix 1 built in UE 5.8.3 and all **84 native Probe Automation Framework tests** passed, alongside successful manual QA of the Dev 15 workspace. Dev 16 adds **9 native tests**, for **93 expected Probe tests**. This new package requires an Unreal compile and Editor manual verification before acceptance.

- **Unified recipes:** The **Recipes** menu now lists both built-in and project-saved recipes, with Save Selection as Recipe and Browse & Manage Recipes. The former "Save Group" control was saving a recipe, but storing it under a separate "Insert Saved" list. Both categories now live together. The existing project-local JSON format remains unchanged.
- **Recipe Browser:** Search, read-only preview, insert, rename, duplicate and delete saved recipes. Built-in recipes are immutable. Name/path validation, collision guards and deletion confirmation are retained. Saved recipes never grant execution permissions.
- **Actual group headers:** Contiguous named groups have expandable headers and context-sensitive Group Actions for rename, copy, duplicate, run and Save Group as Recipe. No Test/Pack asset migration.
- **Contextual authoring:** Each Test step keeps its select/Enabled controls and displays secondary actions under a single `...` menu. Multi-step editing appears only when the user selects steps; Preflight, Recipes, Group Outline and Step Finder are organized around the Steps heading.
- **Compact overview:** Readiness, step count, enabled state and authorization are visible without the long workflow settings block. Expand Overview & Workflow Settings for the original controls.
- **Flexible diagnostics:** Results is a resizable pane that collapses to one status strip while idle and expands when execution starts. JSON export and Rerun Problems are in the Results header. Live progress is visible while running.
- **Native navigation:** Related toolbar navigation is grouped together. Ctrl+F focuses Step Finder when the Probe workspace receives the shortcut.

**Safety:** No changes to serialized Test or Pack structures, runtime authorization, history, runner, existing assertions/templates or other plugins. Source-only offline gates pass; no local UE compilation was performed.

Docs: [Dev 16 Guide](Docs/WORKSPACE_AND_RECIPES_0154_DEV16.md), [Dev 16 QA Checklist](Docs/QA_CHECKLIST_0154_DEV16.md), [1.0 Readiness Audit](Docs/PROBE_1_0_READINESS_AUDIT.md).

---

# Probe 0.15.4-dev15-hotfix1

**Hotfix 1 (UE 5.8.3):** Corrected the Step Finder `SSearchBox` construction. The UE 5.8 widget does not expose a `.Text` Slate argument; Probe now restores its search text via `SetText()` after building the widget. No Test/Pack asset serialization or runner behavior was changed.

## Dev 15: Project QA Browser & Workflow Authoring

**Verified baseline:** Dev 14 Hotfix 1 compiled successfully in UE 5.8.3, passed **73/73 Probe native Automation Framework tests**, and passed the user's manual verification. Dev 15 adds **11 native tests**, bringing the expected suite to **84**, and requires fresh UE compilation and Editor verification.

- **QA Browser:** Unified searchable Test Asset and Pack inventory, Tools > Testing > Probe QA Browser, also accessible from both existing workspaces. Uses Asset Registry metadata for initial discovery and reuses an existing window.
- **Project readiness dashboard:** Explicit read-only Scan Visible / Scan Selected / Scan All, with Ready, Warnings, Blocked, Disabled and Not Scanned states, separate Test/Pack filtering, and useful per-asset diagnoses. Uses Dev 14's existing preflight so there is only one authoring policy.
- **Batch QA workflows:** Select visible entries, inspect selected entries independently of filters, copy filtered investigations, export CSV, open in the appropriate workspace, and optionally execute only explicitly selected, previously scanned Ready Test Assets.
- **Test Step Finder and Group Outline:** Live search by assertion, target, group, expected value and description without modifying step order, plus direct group navigation.
- **Saved reusable recipes:** Save a contiguous group of steps under a project-local name; insert in other Test Assets as a single undoable edit. Insertion always sets Needs Configuration for review and never copies permissions. Uses the existing versioned ProbeStepGroup/1 JSON format under `Saved/Probe/Recipes/`.
- **1.0 readiness audit:** A release-blocker checklist separated from work best scheduled after 1.0.

No changes to serialized Test/Pack structures, runtime permissions, existing history format or engine source. Tests use only Unreal's native Automation Framework. Initial asset discovery is metadata-only; requested preflight can load assets but never executes test steps.

Documentation: [Dev 15 Guide](Docs/QA_BROWSER_AND_WORKFLOWS_0154_DEV15.md), [QA Checklist](Docs/QA_CHECKLIST_0154_DEV15.md), [1.0 Readiness Audit](Docs/PROBE_1_0_READINESS_AUDIT.md).

---

# Probe 0.15.4-dev14

## Dev 14: Test Authoring & Execution

**Baseline:** Dev 13 Hotfix 2 was compiled and verified in Unreal Engine 5.8.3: 65/65 native Probe tests, plus 3 external Metric/Orbit tests; the History Browser manual UI checks also passed. This Dev 14 source package has **73 registered native Probe tests** and requires a fresh UE 5.8.3 build and test run.

This pass adds six authoring/execution improvements without changing serialized UProbeTestAsset/UProbeTestPack structures or existing enum values:

- **Full Test Preflight**: read-only validation of required fields, enabled/disabled steps, explicit UI/actor permissions, named lifecycle blocks, and enabled parameter variations. The existing configuration validator was shared rather than duplicated. Preflight reports reasons; it does not execute or automatically authorize anything.
- **Bulk Step Editing**: independently select steps (not the same as enabled), select all/none, toggle enabled state, move selected blocks, duplicate, remove with confirmation, and assign group names. Mutations are one native Unreal undo transaction per action. Duplicate and selection limits are checked before editing.
- **Reusable Test Recipes**: append ready-to-customize multi-step Plugin Health, Editor State, Read-Only UI, and Transient Fixture patterns. The Test is automatically marked Needs Configuration for author review. Existing permissions are never changed. Recipes cannot be appended behind an existing Cleanup lifecycle block.
- **Live Execution Monitor**: native Slate progress bar and status showing the current test, step count, elapsed time, completed test count, problem count, and stop-after-test request. Progress notifications are supplied by the same async runner as the native UI automation. Completion is still handled on the Editor thread.
- **Jump to First Failure**: from Results, open the first failed/errored step and scroll the Test Definition to it. Existing individual Edit Step buttons remain.
- **Pack Readiness Overview**: preview dependency-order validity, missing Test references, member configuration and authorization, and safe retry eligibility before execution. Existing Pack run behavior is unchanged, even for legacy Packs.

No new persistent formats, new plugin dependencies, or external automation drivers. Probe tests continue to use Unreal Engine's native Automation Framework.

See [Dev 14 User Guide](Docs/TEST_AUTHORING_AND_EXECUTION_0154_DEV14.md) and [Dev 14 QA Checklist](Docs/QA_CHECKLIST_0154_DEV14.md).

---

# Probe 0.15.4-dev13-hotfix2

## Dev 13 Hotfix 2: native recurring-failure fixture safety

Fixes the UE 5.8.3 Editor crash in `Probe.Editor.Dev13RecurringFailures` caused by passing `First.Results[0]` directly into `First.Results.Add(...)`. UE detects the source object is owned by the same `TArray` being modified and asserts before the test can complete. The fixture now makes a standalone `FTestResult` copy before insertion, preserving the two-failures-in-one-run test scenario. Also guards index-based inspection of generated comparison/incident arrays so an unexpected zero-result condition reports an Automation Framework failure rather than crashing.

Adds an independent offline source gate (`Tools/ValidateProbeArrayAliasing.py`) to catch same-container `Add`/`Emplace`/`Insert` patterns in native automation fixture source, with positive and negative self-checks. All 65 existing native tests remain registered; production History Browser, history storage, Test Assets, Packs and runner logic are unchanged. Dev 13 Hotfix 1 built and its Browser construction test passed in UE 5.8.3. **Dev 13 Hotfix 2 was subsequently verified: 65/65 native Probe tests plus the History Browser manual checks.** See [Dev 13 Hotfix 2 QA](Docs/QA_CHECKLIST_0154_DEV13_HOTFIX2.md).

# Probe 0.15.4-dev13-hotfix1

## Dev 13 Hotfix 1: UE 5.8 Slate browser compile correction

Fixes the two errors from the user's UE 5.8 build in `ProbeDev13HistoryBrowser.h`:
- Removes one extra `]` at the end of the browser splitter's right panel; the final `]` for `ChildSlot` stays on the next line.
- Passes `Probe::StatusName(Test.Status)` directly to `FString::Printf` instead of dereferencing the returned `const TCHAR*` (which produces a single `TCHAR` and fails UE's checked `%s` formatting).

The History Browser, test suite, and saved history data format are unchanged. Prior Dev 12 baseline: 60/60 native Probe tests. Dev 13 Hotfix 1 requires a fresh UE 5.8.3 compile and native test verification before acceptance.


## Dev 13: dedicated History Browser and regression investigation

**Last native verified baseline: Dev 12 60/60 Probe tests in UE 5.8.3**, plus the user's confirmation that the Dev 12 new tests succeeded. Dev 13 has five new native tests (65 expected total) and requires UE compile/runtime verification.

The History dropdown is now grouped into four short categories, with no raw list of old runs. **Open History Browser...** provides live search and filters, per-run test and step diagnostics, run-to-run change previews, baseline pinning and individually confirmed run deletion. **Regression Analysis** shows recurring failure signatures, flaky tests, and failure-rate trends. Filtered investigation reports can be copied, and trends can be exported as CSV. All data stays under `Saved/Probe`; the existing versioned run file format, 10-run default retention and 64 MiB cap remain unchanged.

See [Dev 13 History Browser and Investigation](Docs/HISTORY_BROWSER_AND_INVESTIGATION_0154_DEV13.md) and [Dev 13 QA Checklist](Docs/QA_CHECKLIST_0154_DEV13.md).

# Probe 0.15.4-dev12

## Dev 12: bounded history, confirmed deletion and historical trend analysis

Probe now registers **60 native Automation Framework cases** (the 57/57 UE 5.8.3-verified Dev 11 baseline plus three new tests). Dev 11's successful manual persistence/restart verification establishes the baseline for this pass. Dev 12 has not yet been compiled or tested in UE 5.8.3.

Run history defaults to **10 project-local run snapshots**. In **History > Maximum Saved Runs**, choose Off (no future saves, old files preserved), 5, 10, 20, 30, or 50. Retained run files are further bounded by a **64 MiB total** size cap; each run remains subject to the pre-existing 12 MiB file size limit. Reducing a nonzero retention limit prunes older runs immediately. Run history is stored in `<Project>/Saved/Probe/History/` rather than packaged content or assets. Clearing history requires an explicit Yes/No confirmation and deletes only Probe history snapshots and the pinned baseline; manual exports in `Saved/Probe/Reports/` and unrelated files are preserved.

An older run may now be **pinned** as the default comparison baseline across Editor restarts. The pin is kept as one separate JSON snapshot outside the rolling history limit (up to 12 MiB). **Use Latest Run as Baseline** unpins it. The History menu also supports filtering by Test Asset, Pack (for new Dev 12 snapshots), variation name, and run outcome. Reopen the menu after changing the text filter. **View Failure Trends**, **Copy Trend Report**, and **Export Trend Report** analyze saved historical runs by test and variation. Failure rates count pass versus fail/error, excluding skipped outcomes; a flaky classification requires at least two pass/fail transitions, so a single regression is not mislabeled flaky. History trends are limited to retained evidence, not all runs ever made. Trend report exports are explicitly initiated, not automatically produced.

See [Dev 12 history guide](Docs/HISTORY_TRENDS_AND_RETENTION_0154_DEV12.md) and [Dev 12 QA checklist](Docs/QA_CHECKLIST_0154_DEV12.md).

**Verification:** Dev 11: **57/57 native tests passed** in UE 5.8.3, plus manual run history validation. Dev 12: offline source gates passed; pending UE compile and native runtime testing.

---


Probe is an **Unreal Engine 5.8.3 Editor-only QA plugin** for creating, executing, and organizing QA checks against plugins, Editor UI registrations, assets, actor state, reflection, and properties. Probe also supports safe temporary UObject fixtures, reversible Editor selection actions, and one-click plugin QA Pack generation. This source package targets Windows 64-bit; no engine source modifications.


## Dev 11: persistent run history and historical comparisons

Dev 11 registers **57 native Automation Framework cases** (the 54/54 verified Dev 10 baseline and three new history regressions). Probe now persists **completed run snapshots** under `<Project>/Saved/Probe/History/` in a versioned JSON format. The last **30** runs are retained, and malformed/unsupported snapshots are ignored without affecting test execution. The new **History** toolbar menu lets authors inspect up to 12 recent runs, see their saved step diagnostics, and select an earlier run as the comparison baseline. On Editor restart, Probe recovers the latest valid run automatically for the next regression comparison. History browsing never changes Test Assets, Packs, authorization, or test ordering.

Snapshots include UTC time, Unreal version, Test Asset/variation identity, results, retries, timings, and per-step data. File size is capped at 12 MB, and a persistence error never changes a test outcome. Explicit historical baseline selection applies to the current workspace session only. See [Persistent History](Docs/PERSISTENT_HISTORY_0154_DEV11.md) and [Dev 11 QA](Docs/QA_CHECKLIST_0154_DEV11.md).

**Verification:** Dev 10 passed **54/54 native Probe tests** in UE 5.8.3, based on the user's November 2, 2026 run. Dev 11 awaits UE compilation and native execution.

## Dev 10: Pack execution policy, prerequisites and run comparisons

Dev 10 registers **54 native Automation Framework cases** (50 verified in Dev 9 Hotfix 1 plus four new native policy/report tests) with the same **233 assertions** and **154 templates**. Probe Packs can opt into **Stop on Failure**, **Retry Read-Only Once**, and explicit earlier-member **Requires** dependencies. The native Pack manager provides policy checkboxes and a per-member prerequisite menu. Tests with blocked prerequisites are Skipped with an explanation. Safe retries only apply to failed, built-in, read-only Test Assets; live UI input, selection, fixtures, custom providers, and tests with elevated authorizations are never retried automatically. Existing Packs retain Continue and no retry by default.

Probe reports compare the latest completed run with the previous run in the current Editor workspace session. Results include attempt counts, setup/cleanup/assertion/infrastructure classification, and JSON export metadata. Comparison history is session-only and does not modify project files. The original full-run success message and every saved assertion ordinal remain unchanged. See [Dev 10 Pack policies](Docs/PACK_EXECUTION_AND_HISTORY_0154_DEV10.md) and [Dev 10 QA](Docs/QA_CHECKLIST_0154_DEV10.md).

**Verification:** Dev 9 Hotfix 1 passed **50/50** Probe native tests in UE 5.8.3. Dev 10 subsequently passed **54/54** Probe native tests in UE 5.8.3 based on the user's November 2, 2026 run.

## Dev 9 Hotfix 1: lifecycle regression fixture correction

The initial Dev 9 build compiled and passed **49/50 native Probe automation cases** in UE 5.8.3. The only failure, `Probe.Editor.Dev9LifecyclePolicy`, was caused by an invalid test fixture: it expected a three-step *contiguous* Cleanup suffix to be rejected as disjoint. The production lifecycle validator correctly accepted that layout. This hotfix fixes the test to check both valid contiguous cleanup suffixes and **genuinely separated** cleanup blocks (`Cleanup`, `Verify`, `Cleanup`), including an explicit diagnostic assertion. **No runner, serialized Test Asset data, permissions, or lifecycle execution behavior changed.** Hotfix 1 subsequently passed 50/50 native Probe tests in the user's UE 5.8.3 Editor. See [Dev 9 Hotfix 1 QA](Docs/QA_CHECKLIST_0154_DEV9_HOTFIX1.md).

## Dev 9: scenario lifecycle and parameter variations (development build)

Probe supports **50 native Automation Framework regression cases** (44 previously verified plus six new cases), **233 built-in assertions**, and **154 templates**. Test Assets may optionally assign a contiguous first **Setup Group** and last **Cleanup Group**. On a failure, remaining verification steps are marked Skipped, while cleanup still executes in the same fixture context. A cleanup failure cannot hide the first failing assertion; later cleanup steps continue. Test-scoped group execution and unconfigured legacy assets retain their existing behavior.

Test Assets can define named **parameter variations** using `Key=Value` bindings and `${Key}` placeholders for their Target, Expected Class, Property Name, Expected Value, and Description fields. **Run Variations** executes enabled cases separately. Expansion into Run All and Probe Packs is opt-in per Test Asset. The runner never modifies saved step definitions or grants Editor input/actor privileges through variations. Result names, retries, and JSON include variation provenance. See [scenario usage](Docs/SCENARIOS_AND_VARIATIONS_0154_DEV9.md) and [Dev 9 QA checklist](Docs/QA_CHECKLIST_0154_DEV9.md).

**Verification:** Dev 8 passed **44/44 Probe native tests** in UE 5.8.3 based on the November 1, 2026 run supplied by the user. Dev 9 initially passed **49/50** because of a test-fixture error; Hotfix 1 then passed **50/50** in UE 5.8.3.

## Dev 8: failure triage and group maintenance (development build)

Probe now includes **233 built-in assertions**, **154 templates**, and **44 native Automation Framework cases**. In Results Explorer, **Problems Only** filters individual step diagnostics to failures/errors, **Copy Failures** produces a compact, actionable issue report, **Retry Failed Group** retries the contiguous group containing the first failed step, and **Rerun Full** executes the complete Test Asset. Group retry never reruns earlier setup implicitly and does not grant permissions. It rejects changed assertion types, changed group names, and disabled failed steps. The Test Definition editor can **Duplicate Group** in place with a distinct name, preserving step settings and participating in the Unreal transaction system.

**Verification:** Dev 7 Hotfix 1 passed **41/41 native Probe regression tests in UE 5.8.3**, based on the user's October 31, 2026 results. Dev 8 subsequently passed **44/44** Probe native regression tests in Unreal. See [Dev 8 usage](Docs/FAILURE_TRIAGE_AND_GROUP_MAINTENANCE_0154_DEV8.md) and [Dev 8 QA](Docs/QA_CHECKLIST_0154_DEV8.md).

## Dev 7 Hotfix 1: full-run diagnostic compatibility

Dev 7 introduced scoped and disabled-step summaries, but accidentally replaced the long-standing `All N steps passed` success message even for complete, fully passing tests. This broke two independent Smoke Gate diagnostic checks, although the underlying fixture assertions passed. Complete all-passed runs now retain `All N steps passed`; whole runs with disabled steps report passed/disabled counts; and scoped runs keep their inclusive range and counts. The same summary policy is used in synchronous and asynchronous execution. The independent Smoke Gate oracle is unchanged. Native regression coverage now checks these cases, and the source validator rejects removal of the legacy success summary. See [Dev 7 Hotfix 1 QA](Docs/QA_CHECKLIST_0154_DEV7_HOTFIX1.md).

**Verification:** Dev 7 Hotfix 1 passed 41/41 native Probe tests in UE 5.8.3, including the repaired Smoke Gate.

## Dev 7: reusable step groups and controlled long runs

Probe contains **233 built-in assertions**, **154 templates**, and **41 registered native Automation Framework tests** (38 from the verified Dev 6 Hotfix 1 baseline plus three Dev 7 regressions). Authors can name **consecutive steps as a group**, **Copy Group** to a versioned clipboard block, and **Paste Steps** into another Test Asset without copying Editor input or actor authorization. **Run Group** executes only that contiguous set of steps; preceding setup outside the range is intentionally not executed. Step checkboxes disable assertions without deleting them; disabled steps report **Skipped** and do not take actions.

**Stop After Test** safely finishes the current queued test (including native Slate cleanup), skips unstarted tests, and is not a hard interrupt. Results Explorer displays step timings and group names; clipboard and JSON reports carry group names. Legacy saved Test Assets retain their enum ordinals and default enabled behavior. The QA and usage instructions are in [Dev 7 step groups and run control](Docs/STEP_GROUPS_AND_RUN_CONTROL_0154_DEV7.md) and [Dev 7 QA checklist](Docs/QA_CHECKLIST_0154_DEV7.md).

**Verification:** Dev 6 Hotfix 1 passed **38/38** Probe tests. The original Dev 7 compiled in UE 5.8.3 and passed **40/41** Probe tests; only the Smoke Gate diagnostic summary contract failed. Dev 7 Hotfix 1 subsequently passed 41/41 native Probe tests.


## Dev 6 Hotfix 1: native input reliability and catalog tests

Two test-only catalog expectations were still set to 223 after the Dev 6 assertion total increased to 233. Both now check the full catalog. The native append action now navigates to the end using the conventional **Ctrl+End** chord after a brief focus settle, using the same input implementation for named Details search and tagged Slate widgets. Each input failure reports the specific stage (click/focus, caret navigation, selection, or typing), and the Dev 6 automation test prints per-step diagnostics if text input fails. This source hotfix still requires UE 5.8.3 runtime verification; the 33/33 Dev 5 Hotfix 2 baseline remains the last fully verified release.

## Native Editor UI: conditional waits and text automation

Probe includes **233 built-in assertions** and **154 templates**. Test Assets can use stable `id:<AutomationDriverID>` or `details:search` selectors to inspect live widgets, wait for them to appear/disappear/become interactable, and verify text as the Editor changes. New text actions **Append Text** and **Clear Widget Text** require explicit **Authorize UI Input**. Read-only waits and assertions never authorize or emit clicks. Each timeout reports last-observed state and elapsed duration. The **Widget Targets** browser can create quick readiness, disappearance, and text-change waits, and **Check Targets** distinguishes expected absence from missing targets that should be present.

The native Automation Framework tests use disposable Slate fixtures, including delayed state changes, intentional timeout reporting and authorized text editing. The complete Dev 6 suite registers **38 Probe tests**. **Last verified baseline:** Dev 5 Hotfix 2 passed **33/33** Probe tests in UE 5.8.3; Dev 6 still requires native compilation and runtime verification. See [native UI conditions](Docs/NATIVE_UI_CONDITIONS_0154_DEV6.md) and [Dev 6 checklist](Docs/QA_CHECKLIST_0154_DEV6.md).

## 0.15.4-dev5-hotfix2: Authoring acceptance automation

Extends the successfully tested Dev 5 Hotfix 1 baseline (30/30 native tests in UE 5.8.3) with **three additional automated cases**: live Widget Targets browser filters, an isolated authored Test Asset/preflight exercise with temporary tagged Slate widgets, and asynchronous execution of steps constructed through the same quick-step policy as the UI. The full suite now registers **33** tests. No new assertion types, serialized enum changes, or Test Asset schema changes are involved.

The **Check Targets** preflight now correctly reports `details:search` separately as *native Details (deferred)* rather than calling it unavailable. Browser search and filtering now share testable logic with their regression cases. Preflight remains read-only and does not modify readiness or invoke UI input. A disposable Test Asset's step values are also verified through UObject duplication, but this does **not** replace manual disk save/reopen and undo/redo verification.

**Verification:** Dev 5 Hotfix 1 is user-verified at 30/30. This Hotfix 2 build is source-checked only, pending Unreal 5.8.3 compilation and `Automation RunTests Probe` (expected 33/33). See [Dev 5 acceptance automation checklist](Docs/QA_CHECKLIST_0154_DEV5_ACCEPTANCE_AUTOMATION.md).

## 0.15.4-dev5: Inspect and author from live widget targets

**Quick assertion authoring:** The **Widget Targets** browser can create ready-to-configure native UI assertions directly in the selected Probe Test Asset, including **Exists**, **Visible**, **Interactable** and **Wait For Visible**. For directly tagged `STextBlock` or `SEditableTextBox` controls, it also creates **Text Equals Current Text** and **Wait For Current Text** assertions with captured Expected Value. Changes are transactional, undoable, dirty the asset, and preserve its existing steps. Duplicate IDs, including IDs differing only in capitalization, and stale widget targets cannot be used to create steps. The browser also supports **Unique only** and **Duplicates only** filters and text previews.

**Target inspection:** Every native Widget Selector has an **Inspect** menu to verify exact ID resolution, effective state and available direct text, with **Copy Observed Text** and **Set as Expected** for text assertions. A **Check Targets** toolbar action validates the current Test Asset's native UI selectors in a read-only snapshot. Missing panels are reported as currently unavailable, not as failures or permanent readiness blockers. `details:search` remains an execution-time special selector. Read-only-only assets show an explanatory input authorization hint.

**Source and safety:** `ProbeWidgetDiscovery.h` now exposes shared pure resolution diagnostics and direct-text snapshots; `ProbeWidgetTargetAuthoring.h` holds quick-step safety policy. Three new native tests verify selector edge cases, real tagged Slate text widgets and safe quick-step generation. The suite increases from the **27/27 verified Dev 4 baseline** to **30** registered cases requiring a new UE 5.8.3 runtime verification. No Test Asset migrations, new input privileges or engine changes.

See [Widget Target Authoring](Docs/WIDGET_TARGET_AUTHORING_0154_DEV5.md) and [Dev 5 QA checklist](Docs/QA_CHECKLIST_0154_DEV5.md).

## 0.15.4-dev4: Discoverable native widget targets

**Live Widget Targets** introduces a read-only Slate target browser in the **Test Definition** toolbar, and a context-sensitive **Find...** picker next to **Widget Selector** fields for native click, type, assertion, and wait steps. The browser searches currently visible Slate windows for *explicit* `FDriverIdMetaData` identifiers, showing their owning window, widget type, effective visibility/enabled state, and matching `id:` selector. Search filtering and manual **Refresh** let developers open their plugin UI, find a stable target, and insert it without transcribing widget IDs. The global browser offers **Copy** even without a selected Test Asset. The per-step **Use** button updates the step through Probe's existing undo/redo and dirty-state machinery.

**Safety:** untagged widgets are never presented as selectable targets; duplicate IDs are flagged as ambiguous and cannot be copied/inserted. The browser is strictly observational, does not send input or modify selection, and has bounded traversal. The special native Details selector `details:search` remains directly supported and can be entered manually. This pass adds stable Automation Driver IDs to Probe's own **Run Selected**, **Add Step**, and **Refresh Tests** controls for dogfooding and plugin-author examples. No Test Asset schema or step enum changes.

**Regressions:** `Probe.Editor.WidgetDiscoveryUniqueIds` verifies tagged discovery, selector formation and owning-window identity without counting untagged widgets. `Probe.Editor.WidgetDiscoveryRejectsDuplicates` verifies collisions are reported as unsafe. The Dev 4 native automation suite contains **27 cases** and passed **27/27** in the user's UE 5.8.3 Editor, including manual picker, Copy, Find/Use and save/reopen checks.

See [Widget Target Discovery](Docs/WIDGET_TARGET_DISCOVERY_0154_DEV4.md) and [Dev 4 QA checklist](Docs/QA_CHECKLIST_0154_DEV4.md).

## 0.15.4-dev3: Native Widget Inspection and bounded waits

**Twelve new authorable native UI steps** extend Probe from clicking and typing into **read-only Slate widget inspection**: widget exists/does not exist, visible, interactable, text equals/contains/not-equals, wait for widget visibility or exact text, plus Details 1 search contains/not-equals and inactive filter checks. Use stable `id:<automation ID>` or `details:search` selectors. Only the original click/type steps require **Authorize UI Input**; read-only checks do not inject input. Native UI steps remain asynchronous so Slate can keep processing work while the checks run.

**Bounded waits** use a separate, serialized **Wait Timeout (s)** field (default 5, range 0.1–30 seconds) with a native Slate numeric control and validation. Timeout, missing-widget, and observed-text messages are included in per-step results. Existing enum ordinals and Test Asset data are preserved by appending the twelve new enum values and an optional step property. The catalog now has **223 built-in assertions** and retains **154 templates**.

**Native authoring UI** groups new steps into **Native Widget Assertions**, **Native Widget Waits**, and **Details Panel**, with actionable selector labels, suitable initial values and wait-duration summaries. The existing **Native Widget Automation** grouping remains for click/type. A new independent `Probe.Driver.AuthoredWidgetInspection` Automation Framework spec exercises nine read-only steps and a deliberately failing text assertion against disposable native Slate controls. The authored Details spec now verifies six steps. The optional external UI gate requires both new spec cases. Source validation tools have been updated to the Hotfix 2 C++ filename `ProbeTestExecution.cpp`.

**Previously verified baseline:** Dev 2 Hotfix 2 compiled successfully in the user's UE 5.8.3 Editor, passed all **23/23** native tests, and passed saved Test Asset, persistence, repeated execution, multiple-Test Pack, intentional-failure and state-restoration checks on October 28, 2026. **Dev 3 subsequently passed 25/25 automated tests** in the user's UE 5.8.3 Editor. The engine is unchanged.

See [Native Widget Inspection](Docs/NATIVE_WIDGET_INSPECTION_0154_DEV3.md) and [Dev 3 QA checklist](Docs/QA_CHECKLIST_0154_DEV3.md). This version is a development build; complete the validation checklist before treating it as a stable release.

## 0.15.4-dev2: Native Details search cleanup correction (validated baseline)

October 28, 2026 UE 5.8.3 results from **0.15.4-dev1**: `Probe.Runner.NativeUIAuthoringSafety` PASS, `Probe.Driver.AuthoredDetailsSearch` PASS, `Probe.Driver.SlateButton` PASS, `Probe.SelfTest` 15/15 PASS with zero oracle mismatches. `Probe.Driver.EditorDetailsSearch` FAILS when the original Details panel had no objects selected: the visible search box returned to empty, but `IDetailsView::HasActiveSearch()` remained true after the 1600 ms cleanup settle.

**Correction:** All three native Details-restoration paths (independent Details spec, Probe-authored async runner, and authored Details spec cleanup) now share `ProbeDetailsSearchRestore.h`. They use `IDetailsView::ClearSearch()` for an originally empty search rather than relying on `SetSearchText(FText::GetEmpty())`, and they restore any test-substituted object set **before** restoring the native search state. The standalone spec also uses `ClearSearch()` when establishing its unfiltered baseline. Result assertions remain strict; unexpected filter-state mismatches still fail, and the runner now emits original/current text and active-state diagnostics.

**The Dev 2 Hotfix 2 baseline was subsequently compiled and runtime-verified in UE 5.8.3.** Its 211 registered steps, 154 templates, the 15 Smoke Self-Test cases, and Automation Driver threading model were retained. See `Docs/QA_CHECKLIST_0154_DEV2.md` for focused regression scenarios. Original editor tab-focus limitation remains.

## 0.15.4-dev1: Authorable native Editor UI automation (development validation)

**New experimental authorable steps in the normal Probe Test editor:** `UI Click Widget`, `UI Type Text Into Widget`, `UI Details Search Is Visible`, `UI Details Search Text Equals`, and `UI Details Filter Is Active`. Inputs use stable selectors: `details:search` identifies the **existing native Level Editor Details 1** search field, while `id:<automation ID>` identifies a widget carrying a Slate Automation Driver ID. No raw screen coordinates or unscoped text search are supported.

UI actions use a new **asynchronous runner**: Automation Driver clicks and types from a worker thread while Editor/Slate continues ticking. A test may mix traditional synchronous assertions with native UI steps. The Probe workspace queues Test and Pack runs serially and only marks them complete once their callback returns. Direct C++ `Probe::RunTest()` deliberately refuses UI steps; use `Probe::RunTestAsync()` for tests containing native UI actions.

**Safety and cleanup:** `Allow Editor UI Input` is a separate, opt-in permission on each Test Asset, exposed in Probe's authoring panel when relevant. The runner saves/restores Details search text, active filter state, and keyboard focus, waiting 1600 ms for deferred Slate search processing. It does not claim to reverse arbitrary button presses or generic `id:` text edits, or to restore a previously active sibling dock tab. Use a disposable Editor session/map for potentially destructive controls. The driver is left enabled if another subsystem had enabled it before the test.

An unattended-safe native regression `Probe.Runner.NativeUIAuthoringSafety` checks permission denial, synchronous runner refusal and invalid selectors. The independent interactive regression `Probe.Driver.AuthoredDetailsSearch` creates an in-memory Probe Test with real authorable steps, verifies four steps executed and passed, and asserts restoration. It is a third interactive UI requirement in the independent release-gate validator. The earlier `Probe.Driver.EditorDetailsSearch` and `Probe.Driver.SlateButton` tests are preserved. The 15-case Smoke suite, 154 templates, and first 206 serialized step ordinals are unchanged; the catalog now has 211 steps.

**Run validation in UE 5.8.3:** `Automation RunTests Probe.Driver.AuthoredDetailsSearch`, then `Automation RunTests Probe.Driver.EditorDetailsSearch`, `Automation RunTests Probe.Driver.SlateButton`, and `Probe.SelfTest`. See [authoring guide](Docs/NATIVE_UI_AUTHORING_0154.md) and [QA checklist](Docs/QA_CHECKLIST_0154.md). Native UI automation requires a visible, interactive Editor and is not part of unattended Gauntlet/command-line validation.

**Current evidence: source consistency and external validator self-tests only.** This development ZIP is **not claimed to compile or pass interactive runtime tests in UE 5.8.3** until user logs confirm it.

## 0.15.3-hotfix4: UE 5.8.3 compilation correction

- Fixes the Probe-only C2039 compile error in `ProbeDriverEditorDetails.spec.cpp`: `FTabManager::GetActiveTab()` is unavailable in Unreal Engine 5.8.3.
- Preserves the existing Details-tab lookup/activation, native Automation Driver test, fixture handling, filter assertion, focus restoration, and release-gate behavior.
- **Known limitation:** when the test has to activate a background Details tab, its previously foreground sibling tab cannot be restored through the removed API. The original keyboard focus is still restored when possible. This limitation is preferable to relying on an API that does not exist in UE 5.8.3, and will be addressed separately using an engine-supported tab-state mechanism.
- No changes to Strand, Omni, or any other plugin.

**This update has source-level validation only.** Build in UE 5.8.3 and rerun `Automation RunTests Probe.Driver.EditorDetailsSearch` to validate native behavior.


## 0.15.3-hotfix2: Native Details search behavior and state restoration

- Fixes the real Details search test checking `HasActiveSearch()` too early. Automation Driver types text through the existing Slate field, waits 1600 ms on its worker thread for possible `SSearchBox` deferred change notifications, then verifies the native Details filter state on the Editor thread.
- Starts from a known unfiltered view to ensure the filter assertion cannot pass because an earlier user-entered search was already active.
- Handles no actor/object selection by displaying a transient, in-memory Probe Test Asset in the existing Details 1 panel during the test only. The original displayed objects are restored unconditionally, including on partial setup failure. No level actor or asset file is created.
- Searches for Details 1 in the Details view's hosting tab manager before falling back to global tabs. Missing tab lookup alone is diagnostic, not a failure if Slate interaction works.
- Fixes a dead focus-restoration branch. Original search and Details content restoration remain mandatory.
- Documents that a passing click and matching search text do not alone prove the Details filter changed. The independent `HasActiveSearch()` assertion still must pass.

**UE 5.8.3 compilation and real UI execution of this hotfix have not yet been verified.** Run the native Details test with and without an actor selected; see `Docs/SELF_TEST_0153_HOTFIX2.md`.

## 0.15.3-hotfix1: Native Details widget discovery

The first live UE 5.8.3 run found the native `SSearchBox` in the `IDetailsView` tree, but Automation Driver could not reach it as a visible Slate element. The smoke gate (15/15) and `Probe.Driver.SlateButton` passed; only `Probe.Driver.EditorDetailsSearch` failed.

The updated interactive spec activates the *existing* Details 1 dock tab before driving input (it does not open or create a replacement tab), checks Slate's actual arranged visible widget path, and attempts input after an Editor tick. Cleanup restores the original search text, keyboard focus, and the previously active tab when available. Locator failures produce a specific path/tab diagnostic instead of five cascading input assertions. This hotfix has not been compiled or run in the user's UE 5.8.3 installation. See [hotfix validation](Docs/SELF_TEST_0153_HOTFIX1.md).


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

Read [Docs/EXTENDING_PROBE.md](Docs/EXTENDING_PROBE.md) for test provider modules, assertion and template registration, and optional companion plugin packaging. `Tools/ValidateProbeCatalog.py` is a source-only developer check; normal users do not need to run it. Probe supports **temporary UObject test actions and reversible Editor selection actions**, explicit opt-in transient actor creation, and authorized native Editor UI clicks and typing via its asynchronous Automation Driver runner. Its Test Asset editing supports Undo/Redo. The actor actions require a disposable test map for initial QA.

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

See `Docs/START_PLUGIN_QA.md` for baseline plugin checks, `Docs/START_BEHAVIOR_QA.md` for actor workflow setup and safety boundaries, and `Docs/QA_CHECKLIST_012.md` for the version-specific acceptance tests. Native UI clicking, typing, inspection and waits are supported for tagged controls; plugin-specific workflows such as Solo, Outliner hierarchy locks, and Blueprint interactions still need stable widget IDs or appropriate test adapters.


## Test readiness and world authorization (0.14.0)

Probe displays **Test Readiness** as a status section with **Mark Ready / Mark Needs Review** actions. A test with incomplete inputs cannot be marked Ready. Actor fixture steps display a separate **World Actions** authorization section, with explicit authorize/revoke actions. Actor fixtures are still restricted to actors created by the test and may dirty the current level; use a disposable map.

Add Step, Change Assertion, and Templates now use a shared category/subcategory hierarchy so the menus remain navigable as the library grows. This version includes 206 built-in step types and 154 templates, including eleven more test-owned actor checks and matching workflow templates. Existing serialized enum IDs remain unchanged.

The `Probe.Editor.StepDuplicationSafety` Automation Framework test can be run via Unreal's Output Log command input using `Automation RunTests Probe.Editor.StepDuplicationSafety`.

See `Docs/QA_CHECKLIST_014.md` for first-run checks. Plugin-specific Focus automation will be based on the current Focus source rather than the outdated public repository version.
