# Probe 0.1.0

Probe is an experimental Unreal Engine Editor QA workspace for authoring and running simple, read-only test assets without writing C++.

## Compatibility

- Intended target: Unreal Engine 5.8.3
- Editor only
- Source distribution; **not compiled or validated against an Unreal Engine installation**

## Installation

1. Copy the `Probe` folder into `<Project>/Plugins/Probe/`.
2. Regenerate project files if necessary and compile the Editor target.
3. Enable **Probe** in Edit > Plugins if it is not already enabled, and restart the Editor.
4. Open **Tools > Probe QA**.

## Create a test

1. In the Probe QA tab, select **New Test Asset**.
2. Choose a location, preferably `/Game/ProbeTests`, and name the asset.
3. Select the new test in the dropdown. Its properties appear in the embedded Details view.
4. Add elements to the **Steps** array. Set a step's **Type** and, if relevant, **Target** or **Minimum Actor Count**.
5. Save the asset (Ctrl+S or Save All). Click **Run Selected** or **Run All**.
6. Read pass/fail/skipped/error status and the first failing step in the results area.

The test authoring UI in this version is the standard Unreal Details panel embedded in the Probe tab. It is **not** a drag-and-drop action editor or interaction recorder.

## Supported steps

| Type | Target | Behavior |
| --- | --- | --- |
| Plugin is enabled | Plugin name, e.g. `Probe` | Passes when the named plugin is installed and enabled |
| Class exists | Full class object path or loaded class name | Passes when the class can be resolved |
| Asset exists | Asset object path, e.g. `/Game/ProbeTests/Probe_Test.Probe_Test` | Passes when Asset Registry finds the asset |
| Editor world exists | Not used | Passes when an Editor world is available |
| Actor count at least | Minimum Actor Count integer | Counts actors in the current Editor world |

Steps run sequentially and stop at the first failure. A disabled test is skipped; a test without steps reports Error. Tests do not intentionally change the level or project assets.

## Implementation and current limits

- Uses a native dockable Slate tab, Unreal Data Assets, Asset Registry, and an in-process assertion runner.
- Discovers `UProbeTestAsset` assets indexed by the Asset Registry.
- Reports are **session-only** and displayed in the tab. No persisted history, JSON/HTML export, screenshot capture, or cancellation yet.
- No automatic discovery/execution of arbitrary tests registered with Unreal's native Automation Framework yet. Integrating with that framework is planned, but **is not implemented in 0.1.0**.
- No native Automation Driver, UI replay, Python bridge, Blueprint step extension, test-pack API, test sandbox, or standalone runner yet.
- `TestId` is generated for each newly constructed test asset. Duplicated assets may copy the original ID; uniqueness validation is not implemented yet.
- This version is a **source prototype**. It has not been compiled or smoke-tested in UE 5.8.3 here. Build errors and engine API adjustments may be necessary.

## Suggested first smoke test

1. Create a test asset named `Probe_Smoke`.
2. Add two steps: `Plugin is enabled` with Target `Probe`, then `Editor world exists`.
3. Save and run. Both should pass in an open Editor project with Probe enabled.
4. Change the first step's target to `PluginThatDoesNotExist` and rerun. The test should fail at step 1.
5. Restore `Probe`, save, and rerun to confirm it passes again.

## Next implementation milestones

1. Validate compilation and Editor behavior in UE 5.8.3; resolve any API incompatibilities.
2. Add native Automation Framework discovery/execution and persistent report export.
3. Add stable unique IDs, test validation, richer authoring controls, and a test-step extension API.
4. Add isolated fixture management, transactional tests, and Slate UI interaction coverage.
