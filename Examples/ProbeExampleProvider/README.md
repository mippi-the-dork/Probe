# Probe Example Provider v1.2.0 — Optional Integration

This standalone UE 5.8.3 Editor plugin demonstrates the `Resources/Probe/manifest.json` contract. There is **no dependency on Probe** in its `.uplugin` or `.Build.cs`. Without Probe installed, its JSON resources remain inert and the plugin still loads.

## Features represented

- Built-in aliases and an explicit native `ProbeReadOnly` check
- Read-only JSON Tests, a Template and a Pack
- Authorized test-owned UObject create/assert/destroy lifecycle
- Native reflected async polling, cancellation and cleanup (with explicit authorization)

## Validation and authoring with Dev 19

Once installed as `Plugins/ProbeExampleProvider`, rebuild/restart Unreal Editor and open **Tools > Testing > Probe Providers**. You can search Tests/Packs, preview steps and source paths, edit original JSON using the associated text editor, validate the provider, export unchanged definition JSON, and import **read-only** Tests as disabled Needs Review assets. Action Tests remain portable-only.

Run `Probe.Portable.Validate ProbeExampleProvider` and `Probe.Portable.Refresh`, then use:

```text
Probe.Portable.RunTest ProbeExampleProvider.Available
Probe.Portable.RunPack ProbeExampleProvider.All
Probe.Portable.RunTestActions ProbeExampleProvider.FixtureLifecycle
Probe.Portable.RunPackActions ProbeExampleProvider.Actions
```

Run reports are written by Probe under `<Project>/Saved/Probe/PortableReports`. The provider itself does not write or inspect those reports.

See Probe's `Docs/PROVIDER_AUTHORING_DEV19.md`, `Docs/PORTABLE_PROVIDER_SPEC_1.md` and `Docs/PORTABLE_ACTIONS_DEV18.md` for complete contracts.
