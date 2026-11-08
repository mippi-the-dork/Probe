#include "ProbePortableProviders.h"

#include "ProbeExtensions.h"
#include "ProbeAssertionCatalog.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "ModuleDescriptor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace ProbePortable
{
namespace
{
    TArray<FPortableProvider>& Cache() { static TArray<FPortableProvider> Items; return Items; }
    TArray<FString>& Issues() { static TArray<FString> Items; return Items; }
    TArray<FName>& RegisteredAssertionIds() { static TArray<FName> Items; return Items; }
    TArray<FName>& RegisteredTemplateIds() { static TArray<FName> Items; return Items; }
    TArray<FName>& RegisteredActionIds() { static TArray<FName> Items; return Items; }
    int32& ActiveRuns() { static int32 Count = 0; return Count; }

    bool IsReadOnlyNativeType(EProbeStepType Type)
    {
        // Read-only assertions against *test-owned* fixtures are safe even
        // when portable mutation is disabled; they cannot create fixtures.
        if (Type == EProbeStepType::FixturePropertyEquals
            || Type == EProbeStepType::FixturePropertyNotEquals
            || Type == EProbeStepType::FixtureObjectExists)
        { return true; }
        const int32 Index = static_cast<int32>(Type);
        if (Index >= static_cast<int32>(EProbeStepType::ActorFixtureExists)
            && Index <= static_cast<int32>(EProbeStepType::ActorFixtureScaleNotNearlyEquals)
            && Type != EProbeStepType::ActorFixtureSelect
            && Type != EProbeStepType::ActorFixtureHide
            && Type != EProbeStepType::ActorFixtureShow
            && Type != EProbeStepType::ActorFixtureDestroy
            && Type != EProbeStepType::ActorFixtureSetLocation
            && Type != EProbeStepType::ActorFixtureSetRotation
            && Type != EProbeStepType::ActorFixtureSetScale
            && Type != EProbeStepType::ActorFixtureAttachTo
            && Type != EProbeStepType::ActorFixtureDetach
            && Type != EProbeStepType::ActorFixtureSetLabel
            && Type != EProbeStepType::ActorFixtureSetFolder
            && Type != EProbeStepType::ActorFixtureAddTag
            && Type != EProbeStepType::ActorFixtureRemoveTag)
        { return true; }
        return !((Index >= static_cast<int32>(EProbeStepType::FixtureCreateObject)
                   && Index <= static_cast<int32>(EProbeStepType::FixtureDestroyObject))
            || (Index >= static_cast<int32>(EProbeStepType::SelectionClear)
                   && Index <= static_cast<int32>(EProbeStepType::ActorFixtureScaleNotNearlyEquals))
            || Type == EProbeStepType::UIWidgetClick || Type == EProbeStepType::UIWidgetTypeText
            || Type == EProbeStepType::UIWidgetClearText || Type == EProbeStepType::UIWidgetAppendText);
    }

    bool IsFixtureNativeType(EProbeStepType Type)
    {
        // Explicit allow-list of test-owned operations. Never expose arbitrary
        // live UI input, actor selection, or a plugin-defined native function here.
        switch (Type)
        {
        case EProbeStepType::FixtureCreateObject:
        case EProbeStepType::FixtureSetProperty:
        case EProbeStepType::FixtureDestroyObject:
        case EProbeStepType::ActorFixtureSpawn:
        case EProbeStepType::ActorFixtureSetLabel:
        case EProbeStepType::ActorFixtureSetFolder:
        case EProbeStepType::ActorFixtureAddTag:
        case EProbeStepType::ActorFixtureRemoveTag:
        case EProbeStepType::ActorFixtureHide:
        case EProbeStepType::ActorFixtureShow:
        case EProbeStepType::ActorFixtureSetLocation:
        case EProbeStepType::ActorFixtureSetRotation:
        case EProbeStepType::ActorFixtureSetScale:
        case EProbeStepType::ActorFixtureAttachTo:
        case EProbeStepType::ActorFixtureDetach:
        case EProbeStepType::ActorFixtureDestroy:
            return true;
        default: return false;
        }
    }
    bool IsActorFixtureType(EProbeStepType Type)
    {
        return IsFixtureNativeType(Type)
            && Type != EProbeStepType::FixtureCreateObject
            && Type != EProbeStepType::FixtureSetProperty
            && Type != EProbeStepType::FixtureDestroyObject;
    }

    bool ValidId(const FString& Id, const FString& Provider)
    {
        if (!Id.StartsWith(Provider + TEXT("."), ESearchCase::CaseSensitive)
            || Id.Len() > 128 || Id.Len() <= Provider.Len() + 1) { return false; }
        for (TCHAR C : Id)
        {
            if (!((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'))
                || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('.')
                || C == TEXT('_') || C == TEXT('-'))) { return false; }
        }
        return true;
    }

    bool ReadObject(const FString& Path, const int64 MaximumBytes, TSharedPtr<FJsonObject>& Object, FString& Reason)
    {
        Object.Reset();
        const int64 Size = IFileManager::Get().FileSize(*Path);
        if (Size < 0 || Size > MaximumBytes)
        { Reason = FString::Printf(TEXT("Missing or oversized JSON: %s"), *Path); return false; }
        FString Json;
        if (!FFileHelper::LoadFileToString(Json, *Path)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid())
        { Reason = FString::Printf(TEXT("Invalid JSON object: %s"), *Path); return false; }
        return true;
    }

    bool RequiredString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Value, FString& Reason)
    {
        if (!Object.IsValid() || !Object->TryGetStringField(Key, Value) || Value.IsEmpty())
        { Reason = FString::Printf(TEXT("Missing or invalid string '%s'."), Key); return false; }
        if (Value.Len() > 2048)
        { Reason = FString::Printf(TEXT("String '%s' exceeds length limit."), Key); return false; }
        return true;
    }

    bool OptionalString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Value, FString& Reason)
    {
        if (!Object->HasField(Key)) { return true; }
        if (!Object->TryGetStringField(Key, Value) || Value.Len() > 4096)
        { Reason = FString::Printf(TEXT("Invalid string '%s'."), Key); return false; }
        return true;
    }

    bool Paths(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key,
        TArray<FString>& Out, FString& Reason)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Object->HasField(Key)) { return true; }
        if (!Object->TryGetArrayField(Key, Values) || !Values || Values->Num() > 64)
        { Reason = FString::Printf(TEXT("'%s' must contain at most 64 JSON paths."), Key); return false; }
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            if (!Value.IsValid() || Value->Type != EJson::String)
            { Reason = FString::Printf(TEXT("'%s' contains a non-string path."), Key); return false; }
            const FString Path = Value->AsString();
            if (!ValidateRelativePath(Path, Reason)) { return false; }
            if (Out.Contains(Path)) { Reason = TEXT("Duplicate definition path: ") + Path; return false; }
            Out.Add(Path);
        }
        return true;
    }

    bool ParseStep(const TSharedPtr<FJsonObject>& Object, FProbeTestStep& Step, FString& Reason)
    {
        if (!Object.IsValid()) { Reason = TEXT("A step must be a JSON object."); return false; }
        if (Object->HasField(TEXT("allowEditorUIInput")) || Object->HasField(TEXT("allowTransientEditorActors")))
        { Reason = TEXT("Portable steps cannot grant execution permissions."); return false; }
        FString Type;
        FString Assertion;
        FString Action;
        if (!OptionalString(Object, TEXT("assertion"), Assertion, Reason)
            || !OptionalString(Object, TEXT("action"), Action, Reason)
            || !OptionalString(Object, TEXT("type"), Type, Reason)) { return false; }
        const int32 Selectors = (!Type.IsEmpty() ? 1 : 0) + (!Assertion.IsEmpty() ? 1 : 0) + (!Action.IsEmpty() ? 1 : 0);
        if (Selectors != 1)
        { Reason = TEXT("Each step needs exactly one of 'type', 'assertion', or 'action'."); return false; }
        if (!Assertion.IsEmpty() || !Action.IsEmpty()) { Step.AssertionId = FName(*(Action.IsEmpty() ? Assertion : Action)); }
        else
        {
            const UEnum* Enum = StaticEnum<EProbeStepType>();
            const int64 Ordinal = Enum ? Enum->GetValueByNameString(Type) : INDEX_NONE;
            if (Ordinal == INDEX_NONE || !Enum->IsValidEnumValue(Ordinal)
                || !Enum->GetNameStringByValue(Ordinal).Equals(Type, ESearchCase::CaseSensitive))
            { Reason = TEXT("Unknown built-in assertion type: ") + Type; return false; }
            Step.Type = static_cast<EProbeStepType>(Ordinal);
            const bool bInCatalog = Probe::AssertionCatalog().ContainsByPredicate(
                [Ordinal](const Probe::FAssertionInfo& Info)
                { return static_cast<int64>(Info.Type) == Ordinal; });
            if (!bInCatalog)
            { Reason = TEXT("Assertion type is not registered in Probe's built-in catalog."); return false; }
            // Portable tests start read-only. Action permissions and lifecycle
            // fixture contexts require a separate opt-in contract in Dev 18.
            if (!IsReadOnlyNativeType(Step.Type))
            { Reason = TEXT("Portable test uses a state-changing step; read-only definitions only in ProbeProvider/1."); return false; }
        }
        if (!OptionalString(Object, TEXT("target"), Step.Target, Reason)
            || !OptionalString(Object, TEXT("expectedClass"), Step.ExpectedClass, Reason)
            || !OptionalString(Object, TEXT("property"), Step.PropertyName, Reason)
            || !OptionalString(Object, TEXT("expected"), Step.ExpectedValue, Reason)
            || !OptionalString(Object, TEXT("description"), Step.Description, Reason)
            || !OptionalString(Object, TEXT("group"), Step.GroupName, Reason)) { return false; }
        if (Object->HasField(TEXT("enabled")) && !Object->TryGetBoolField(TEXT("enabled"), Step.bEnabled))
        { Reason = TEXT("Invalid step enabled flag."); return false; }
        double Number = 0;
        if (Object->HasField(TEXT("minimumActorCount")))
        {
            if (!Object->TryGetNumberField(TEXT("minimumActorCount"), Number)
                || !FMath::IsFinite(Number) || Number < 0 || Number > 1000000 || static_cast<double>(static_cast<int32>(Number)) != Number)
            { Reason = TEXT("Invalid minimumActorCount."); return false; }
            Step.MinimumActorCount = static_cast<int32>(Number);
        }
        if (Object->HasField(TEXT("numericTolerance")))
        {
            if (!Object->TryGetNumberField(TEXT("numericTolerance"), Number) || !FMath::IsFinite(Number) || Number < 0)
            { Reason = TEXT("Invalid numericTolerance."); return false; }
            Step.NumericTolerance = Number;
        }
        if (Object->HasField(TEXT("uiWaitTimeoutSeconds")))
        {
            if (!Object->TryGetNumberField(TEXT("uiWaitTimeoutSeconds"), Number)
                || !FMath::IsFinite(Number) || Number < 0.1 || Number > 30.0)
            { Reason = TEXT("UI wait timeout must be 0.1 through 30 seconds."); return false; }
            Step.UIWaitTimeoutSeconds = Number;
        }
        return true;
    }

    bool ParseSteps(const TSharedPtr<FJsonObject>& Object, TArray<FProbeTestStep>& Out, FString& Reason)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Object->TryGetArrayField(TEXT("steps"), Values) || !Values || Values->IsEmpty() || Values->Num() > 256)
        { Reason = TEXT("A Test or Template needs 1-256 steps."); return false; }
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            FProbeTestStep Step;
            if (!Value.IsValid() || Value->Type != EJson::Object
                || !ParseStep(Value->AsObject(), Step, Reason)) { return false; }
            Out.Add(MoveTemp(Step));
        }
        return true;
    }

    bool ParseTest(const TSharedPtr<FJsonObject>& Object, const FString& Provider,
        const FString& Format, FPortableTest& Out, FString& Reason)
    {
        FString FoundFormat;
        if (!RequiredString(Object, TEXT("format"), FoundFormat, Reason) || FoundFormat != Format)
        { Reason = TEXT("Expected ") + Format; return false; }
        if (!RequiredString(Object, TEXT("id"), Out.Id, Reason) || !ValidId(Out.Id, Provider)
            || !RequiredString(Object, TEXT("name"), Out.Name, Reason)
            || !OptionalString(Object, TEXT("description"), Out.Description, Reason))
        { if (Reason.IsEmpty()) { Reason = TEXT("Test ID must be namespaced to its provider."); } return false; }
        if (Object->HasField(TEXT("allowEditorUIInput")) || Object->HasField(TEXT("allowTransientEditorActors")))
        { Reason = TEXT("JSON Test cannot grant live Editor input or Actor permissions."); return false; }
        if (Object->HasField(TEXT("enabled")) && !Object->TryGetBoolField(TEXT("enabled"), Out.bEnabled))
        { Reason = TEXT("Invalid Test enabled flag."); return false; }
        if (!ParseSteps(Object, Out.Steps, Reason)) { return false; }
        const TArray<TSharedPtr<FJsonValue>>* OriginalSteps = nullptr;
        if (Object->TryGetArrayField(TEXT("steps"), OriginalSteps) && OriginalSteps)
        {
            for (int32 Index = 0; Index < OriginalSteps->Num(); ++Index)
            {
                const TSharedPtr<FJsonObject> Source = (*OriginalSteps)[Index]->AsObject();
                if (Source.IsValid() && Source->HasField(TEXT("action")))
                { Out.ActionStepIndices.Add(Index); }
            }
        }
        return true;
    }

    bool ParseAssertion(const TSharedPtr<FJsonObject>& Object, const FString& Provider,
        FAssertionDefinition& Out, FString& Reason)
    {
        FString Format;
        if (!RequiredString(Object, TEXT("format"), Format, Reason) || Format != TEXT("ProbeAssertion/1"))
        { Reason = TEXT("Expected ProbeAssertion/1."); return false; }
        if (!RequiredString(Object, TEXT("id"), Out.Id, Reason) || !ValidId(Out.Id, Provider)
            || !RequiredString(Object, TEXT("name"), Out.Name, Reason)
            || !RequiredString(Object, TEXT("category"), Out.Category, Reason)
            || !RequiredString(Object, TEXT("kind"), Out.Kind, Reason)
            || !OptionalString(Object, TEXT("description"), Out.Description, Reason))
        { if (Reason.IsEmpty()) { Reason = TEXT("Assertion ID must be namespaced to provider."); } return false; }
        if (Out.Kind == TEXT("builtin-alias"))
        {
            if (!RequiredString(Object, TEXT("type"), Out.BuiltInType, Reason)
                || !OptionalString(Object, TEXT("target"), Out.DefaultTarget, Reason)) { return false; }
            FProbeTestStep Dummy;
            TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
            Step->SetStringField(TEXT("type"), Out.BuiltInType);
            if (!ParseStep(Step, Dummy, Reason)) { return false; }
        }
        else if (Out.Kind == TEXT("reflected-readonly"))
        {
            if (!RequiredString(Object, TEXT("class"), Out.ReflectedClass, Reason)
                || !RequiredString(Object, TEXT("function"), Out.ReflectedFunction, Reason)
                || !Out.ReflectedClass.StartsWith(TEXT("/Script/"))
                || Out.ReflectedFunction.Contains(TEXT(".")) || Out.ReflectedFunction.Contains(TEXT("/")))
            { Reason = TEXT("Invalid reflected-readonly class/function declaration."); return false; }
        }
        else { Reason = TEXT("Unknown assertion kind. Expected builtin-alias or reflected-readonly."); return false; }
        return true;
    }

    bool ParseAction(const TSharedPtr<FJsonObject>& Object, const FString& Provider,
        FActionDefinition& Out, FString& Reason)
    {
        FString Format;
        if (!RequiredString(Object, TEXT("format"), Format, Reason) || Format != TEXT("ProbeAction/1"))
        { Reason = TEXT("Expected ProbeAction/1."); return false; }
        if (!RequiredString(Object, TEXT("id"), Out.Id, Reason) || !ValidId(Out.Id, Provider)
            || !RequiredString(Object, TEXT("name"), Out.Name, Reason)
            || !RequiredString(Object, TEXT("kind"), Out.Kind, Reason)
            || !RequiredString(Object, TEXT("permission"), Out.Permission, Reason)
            || !OptionalString(Object, TEXT("description"), Out.Description, Reason))
        { if (Reason.IsEmpty()) { Reason = TEXT("Invalid provider-namespaced action ID."); } return false; }
        if (Out.Kind == TEXT("native-fixture"))
        {
            if (!RequiredString(Object, TEXT("type"), Out.NativeType, Reason)) { return false; }
            const UEnum* Enum = StaticEnum<EProbeStepType>();
            const int64 Value = Enum ? Enum->GetValueByNameString(Out.NativeType) : INDEX_NONE;
            if (Value == INDEX_NONE || !Enum->IsValidEnumValue(Value)
                || !Enum->GetNameStringByValue(Value).Equals(Out.NativeType, ESearchCase::CaseSensitive)
                || !IsFixtureNativeType(static_cast<EProbeStepType>(Value)))
            { Reason = TEXT("Action type is not an allow-listed test-owned fixture operation."); return false; }
            const bool bActor = IsActorFixtureType(static_cast<EProbeStepType>(Value));
            if (Out.Permission != (bActor ? TEXT("transient-actor") : TEXT("transient-object")))
            { Reason = TEXT("Fixture action permission does not match its native operation."); return false; }
        }
        else if (Out.Kind == TEXT("reflected-action") || Out.Kind == TEXT("reflected-async"))
        {
            if (Out.Permission != TEXT("provider-action")
                || !RequiredString(Object, TEXT("class"), Out.ReflectedClass, Reason)
                || !RequiredString(Object, TEXT("startFunction"), Out.StartFunction, Reason)
                || !RequiredString(Object, TEXT("cleanupFunction"), Out.CleanupFunction, Reason))
            { Reason = TEXT("Reflected action requires provider-action permission, class, start and cleanup functions."); return false; }
            if (Out.Kind == TEXT("reflected-async")
                && (!RequiredString(Object, TEXT("pollFunction"), Out.PollFunction, Reason)
                    || !RequiredString(Object, TEXT("cancelFunction"), Out.CancelFunction, Reason)))
            { Reason = TEXT("Async action requires poll and cancel functions."); return false; }
            if (!Out.ReflectedClass.StartsWith(TEXT("/Script/")))
            { Reason = TEXT("Reflected action must declare an Unreal script class."); return false; }
            for (const FString& Symbol : {Out.StartFunction, Out.CleanupFunction, Out.PollFunction, Out.CancelFunction})
            {
                if (Symbol.Contains(TEXT(".")) || Symbol.Contains(TEXT("/")) || Symbol.Contains(TEXT(":")) || Symbol.Len() > 128)
                { Reason = TEXT("Unsafe reflected action function name."); return false; }
            }
        }
        else { Reason = TEXT("Unknown action kind."); return false; }
        return true;
    }

    bool ParsePack(const TSharedPtr<FJsonObject>& Object, const FString& Provider,
        FPortablePack& Out, FString& Reason)
    {
        FString Format;
        if (!RequiredString(Object, TEXT("format"), Format, Reason) || Format != TEXT("ProbePack/1"))
        { Reason = TEXT("Expected ProbePack/1."); return false; }
        if (!RequiredString(Object, TEXT("id"), Out.Id, Reason) || !ValidId(Out.Id, Provider)
            || !RequiredString(Object, TEXT("name"), Out.Name, Reason))
        { if (Reason.IsEmpty()) { Reason = TEXT("Pack ID must be namespaced to its provider."); } return false; }
        FString FailurePolicy;
        if (!OptionalString(Object, TEXT("failurePolicy"), FailurePolicy, Reason)) { return false; }
        if (!FailurePolicy.IsEmpty() && FailurePolicy != TEXT("Continue") && FailurePolicy != TEXT("StopOnFailure"))
        { Reason = TEXT("Pack failurePolicy must be Continue or StopOnFailure."); return false; }
        Out.bStopOnFailure = FailurePolicy == TEXT("StopOnFailure");
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Object->TryGetArrayField(TEXT("tests"), Values) || !Values || Values->IsEmpty() || Values->Num() > 64)
        { Reason = TEXT("A Pack needs 1-64 Test IDs."); return false; }
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            if (!Value.IsValid() || Value->Type != EJson::String || !ValidId(Value->AsString(), Provider)
                || Out.TestIds.Contains(Value->AsString()))
            { Reason = TEXT("Pack contains an invalid, cross-provider, or duplicate Test ID."); return false; }
            Out.TestIds.Add(Value->AsString());
        }
        return true;
    }
}

bool ValidateRelativePath(const FString& Path, FString& Reason)
{
    Reason.Reset();
    if (Path.IsEmpty() || Path.Len() > 180 || !Path.EndsWith(TEXT(".json"))
        || Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("\\"))
        || Path.Contains(TEXT(":")) || Path.Contains(TEXT("//")))
    { Reason = TEXT("Provider definition must be a relative, forward-slash .json path."); return false; }
    TArray<FString> Segments;
    Path.ParseIntoArray(Segments, TEXT("/"), false);
    for (const FString& Segment : Segments)
    {
        if (Segment.IsEmpty() || Segment == TEXT(".") || Segment == TEXT("..") || Segment.StartsWith(TEXT(".")))
        { Reason = TEXT("Provider JSON path contains unsafe segments."); return false; }
        for (TCHAR C : Segment)
        {
            if (!((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'))
                || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('.') || C == TEXT('_') || C == TEXT('-')))
            { Reason = TEXT("Provider JSON path contains unsupported characters."); return false; }
        }
    }
    return true;
}

bool LoadProvider(const FString& Directory, const FString& PluginName,
    FPortableProvider& OutProvider, FString& Reason)
{
    OutProvider = FPortableProvider(); Reason.Reset();
    TSharedPtr<FJsonObject> Manifest;
    if (!ReadObject(Directory / TEXT("manifest.json"), 131072, Manifest, Reason)) { return false; }
    FString Format;
    if (!RequiredString(Manifest, TEXT("format"), Format, Reason) || Format != TEXT("ProbeProvider/1"))
    { Reason = TEXT("Unsupported manifest format. Expected ProbeProvider/1."); return false; }
    if (!RequiredString(Manifest, TEXT("provider"), OutProvider.ProviderId, Reason)
        || !OutProvider.ProviderId.Equals(PluginName, ESearchCase::CaseSensitive)
        || !RequiredString(Manifest, TEXT("version"), OutProvider.Version, Reason))
    { if (Reason.IsEmpty()) { Reason = TEXT("Manifest provider must match owning plugin name."); } return false; }
    if (OutProvider.Version.Len() > 64) { Reason = TEXT("Provider version exceeds 64 characters."); return false; }
    OutProvider.PluginName = PluginName;
    OutProvider.Directory = Directory;
    TArray<FString> Assertions, Actions, Templates, Tests, Packs;
    if (!Paths(Manifest, TEXT("actions"), Actions, Reason)
        || !Paths(Manifest, TEXT("assertions"), Assertions, Reason)
        || !Paths(Manifest, TEXT("templates"), Templates, Reason)
        || !Paths(Manifest, TEXT("tests"), Tests, Reason)
        || !Paths(Manifest, TEXT("packs"), Packs, Reason)) { return false; }
    TSet<FString> AllPaths;
    for (const TArray<FString>* List : {&Assertions, &Actions, &Templates, &Tests, &Packs})
    {
        for (const FString& Item : *List)
        {
            if (AllPaths.Contains(Item)) { Reason = TEXT("Definition file appears in multiple manifest categories: ") + Item; return false; }
            AllPaths.Add(Item);
        }
    }
    if (AllPaths.Num() > 128) { Reason = TEXT("Provider contains too many definition files."); return false; }
    TSet<FString> AllIds;
    auto LoadFiles = [&](const TArray<FString>& Files, const TCHAR* Kind) -> bool
    {
        for (const FString& Path : Files)
        {
            TSharedPtr<FJsonObject> Object;
            if (!ReadObject(Directory / Path, 131072, Object, Reason)) { return false; }
            FString Id;
            if (FCString::Strcmp(Kind, TEXT("assertion")) == 0)
            {
                FAssertionDefinition Definition;
                if (!ParseAssertion(Object, PluginName, Definition, Reason)) { return false; }
                Id = Definition.Id;
                OutProvider.Assertions.Add(MoveTemp(Definition));
            }
            else if (FCString::Strcmp(Kind, TEXT("action")) == 0)
            {
                FActionDefinition Definition;
                if (!ParseAction(Object, PluginName, Definition, Reason)) { return false; }
                Id = Definition.Id;
                OutProvider.Actions.Add(MoveTemp(Definition));
            }
            else if (FCString::Strcmp(Kind, TEXT("pack")) == 0)
            {
                FPortablePack Definition;
                if (!ParsePack(Object, PluginName, Definition, Reason)) { return false; }
                Id = Definition.Id;
                Definition.SourcePath = Path;
                OutProvider.Packs.Add(MoveTemp(Definition));
            }
            else
            {
                FPortableTest Definition;
                if (!ParseTest(Object, PluginName,
                    FCString::Strcmp(Kind, TEXT("test")) == 0 ? TEXT("ProbeTest/1") : TEXT("ProbeTemplate/1"), Definition, Reason))
                { return false; }
                Id = Definition.Id;
                Definition.SourcePath = Path;
                (FCString::Strcmp(Kind, TEXT("test")) == 0 ? OutProvider.Tests : OutProvider.Templates).Add(MoveTemp(Definition));
            }
            const FString CaseFoldedId = Id.ToLower();
            if (AllIds.Contains(CaseFoldedId)) { Reason = TEXT("Duplicate provider definition ID: ") + Id; return false; }
            AllIds.Add(CaseFoldedId);
        }
        return true;
    };
    if (!LoadFiles(Assertions, TEXT("assertion")) || !LoadFiles(Actions, TEXT("action"))
        || !LoadFiles(Templates, TEXT("template"))
        || !LoadFiles(Tests, TEXT("test")) || !LoadFiles(Packs, TEXT("pack"))) { return false; }
    // No orphan/cross-provider references: all dependencies resolve within this plugin.
    TSet<FString> AssertionIds, TestIds;
    for (const FAssertionDefinition& Assertion : OutProvider.Assertions) { AssertionIds.Add(Assertion.Id); }
    TMap<FString, const FActionDefinition*> ActionIds;
    for (const FActionDefinition& Action : OutProvider.Actions) { ActionIds.Add(Action.Id, &Action); }
    for (const FPortableTest& Test : OutProvider.Tests) { TestIds.Add(Test.Id); }
    for (const FPortablePack& Pack : OutProvider.Packs)
    {
        for (const FString& TestId : Pack.TestIds)
        {
            if (!TestIds.Contains(TestId)) { Reason = TEXT("Pack references an undefined Test: ") + TestId; return false; }
        }
    }
    for (TArray<FPortableTest>* List : {&OutProvider.Tests, &OutProvider.Templates})
    {
        for (FPortableTest& Test : *List)
        {
            for (int32 StepIndex = 0; StepIndex < Test.Steps.Num(); ++StepIndex)
            {
                FProbeTestStep& Step = Test.Steps[StepIndex];
                if (Step.AssertionId.IsNone()) { continue; }
                const FString Id = Step.AssertionId.ToString();
                if (AssertionIds.Contains(Id))
                {
                    if (Test.ActionStepIndices.Contains(StepIndex))
                    { Reason = TEXT("Action selector references an assertion instead of an action: ") + Id; return false; }
                    continue;
                }
                if (!Test.ActionStepIndices.Contains(StepIndex))
                { Reason = TEXT("Reflected and fixture actions must use the 'action' selector, not 'assertion': ") + Id; return false; }
                const FActionDefinition* const* Found = ActionIds.Find(Id);
                if (!Found)
                { Reason = TEXT("Test references an undeclared provider assertion/action: ") + Id; return false; }
                const FActionDefinition& Action = **Found;
                Test.bRequiresActionAuthorization = true;
                if (Action.Kind == TEXT("native-fixture"))
                {
                    const int64 Ordinal = StaticEnum<EProbeStepType>()->GetValueByNameString(Action.NativeType);
                    Step.Type = static_cast<EProbeStepType>(Ordinal);
                    Step.AssertionId = NAME_None;
                    Test.bRequiresTransientActors |= IsActorFixtureType(Step.Type);
                }
            }
        }
    }
    for (const FPortableTest& Template : OutProvider.Templates)
    {
        if (Template.bRequiresActionAuthorization)
        {
            Reason = TEXT("Portable templates cannot embed provider actions: saved Test Assets never acquire portable action privileges.");
            return false;
        }
    }
    return true;
}

namespace
{
    Probe::EResultStatus EvaluateReflected(const FPortableProvider& Provider,
        const FAssertionDefinition& Assertion, const FProbeTestStep& Step, FString& Reason)
    {
        check(IsInGameThread());
        bool bOwnModule = false;
        for (const FString& Module : Provider.ModuleNames)
        {
            if (Assertion.ReflectedClass.StartsWith(TEXT("/Script/") + Module + TEXT(".")))
            { bOwnModule = true; break; }
        }
        if (!bOwnModule)
        { Reason = TEXT("Reflected class does not belong to a declared module of the provider plugin."); return Probe::EResultStatus::Error; }
        UClass* Class = FindObject<UClass>(nullptr, *Assertion.ReflectedClass);
        if (!Class) { Class = LoadObject<UClass>(nullptr, *Assertion.ReflectedClass); }
        UFunction* Function = Class ? Class->FindFunctionByName(FName(*Assertion.ReflectedFunction)) : nullptr;
        const UClass* DeclaringClass = Function ? Cast<UClass>(Function->GetOuter()) : nullptr;
        if (!Class || !Function || DeclaringClass != Class
            || !Function->HasAllFunctionFlags(FUNC_Static | FUNC_Native)
            || !Function->HasMetaData(TEXT("ProbeReadOnly")))
        { Reason = TEXT("Reflected check must be a native static UFUNCTION with meta=(ProbeReadOnly)." ); return Probe::EResultStatus::Error; }
        // Only one explicitly documented function signature is supported:
        // static bool Check(FString Target, FString ExpectedValue, FString& OutDetail)
        // The return value is the assertion outcome. No arbitrary UFunction
        // arguments, invocation targets, or Blueprint VM calls are permitted.
        FStrProperty* TargetProperty = nullptr;
        FStrProperty* ExpectedProperty = nullptr;
        FStrProperty* DetailProperty = nullptr;
        FBoolProperty* ReturnProperty = nullptr;
        int32 ParamCount = 0;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Parm)) { continue; }
            ++ParamCount;
            if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                ReturnProperty = CastField<FBoolProperty>(Property);
            }
            else if (Property->GetFName() == FName(TEXT("Target"))
                && !Property->HasAnyPropertyFlags(CPF_OutParm))
            {
                TargetProperty = CastField<FStrProperty>(Property);
            }
            else if (Property->GetFName() == FName(TEXT("ExpectedValue"))
                && !Property->HasAnyPropertyFlags(CPF_OutParm))
            {
                ExpectedProperty = CastField<FStrProperty>(Property);
            }
            else if (Property->GetFName() == FName(TEXT("OutDetail"))
                && Property->HasAnyPropertyFlags(CPF_OutParm))
            {
                DetailProperty = CastField<FStrProperty>(Property);
            }
            else
            { Reason = TEXT("Undeclared parameter in provider function."); return Probe::EResultStatus::Error; }
        }
        if (ParamCount != 4 || !TargetProperty || !ExpectedProperty || !DetailProperty || !ReturnProperty)
        { Reason = TEXT("Provider check must have exact bool(FString Target, FString ExpectedValue, FString& OutDetail) signature."); return Probe::EResultStatus::Error; }
        const int32 BufferSize = FMath::Max<int32>(Function->ParmsSize, Function->GetStructureSize());
        if (BufferSize <= 0 || BufferSize > 4096)
        { Reason = TEXT("Reflected provider parameter buffer exceeds safety limit."); return Probe::EResultStatus::Error; }
        TArray<uint8> Params;
        Params.SetNumZeroed(BufferSize);
        Function->InitializeStruct(Params.GetData());
        *TargetProperty->ContainerPtrToValuePtr<FString>(Params.GetData()) = Step.Target;
        *ExpectedProperty->ContainerPtrToValuePtr<FString>(Params.GetData()) = Step.ExpectedValue;
        Class->GetDefaultObject()->ProcessEvent(Function, Params.GetData());
        const bool bPassed = ReturnProperty->GetPropertyValue_InContainer(Params.GetData());
        Reason = *DetailProperty->ContainerPtrToValuePtr<FString>(Params.GetData());
        Function->DestroyStruct(Params.GetData());
        if (Reason.IsEmpty()) { Reason = bPassed ? TEXT("Provider check passed.") : TEXT("Provider check failed."); }
        return bPassed ? Probe::EResultStatus::Passed : Probe::EResultStatus::Failed;
    }

    // Dev 18: the plugin owns these UFUNCTIONs, and does not include any Probe
    // headers. All calls are native/static, on the Editor game thread, and
    // restricted to the provider's declared modules. Metadata is never itself
    // permission to run: execution additionally requires explicit authorization.
    bool CallAction(const FPortableProvider& Provider, const FActionDefinition& Action,
        const FString& FunctionName, const TCHAR* RequiredMetadata,
        const FProbeTestStep& Step, bool* OutComplete, bool* OutPassed, FString& Detail,
        bool bValidateOnly = false)
    {
        check(IsInGameThread());
        bool bOwned = false;
        for (const FString& Module : Provider.ModuleNames)
        {
            if (Action.ReflectedClass.StartsWith(TEXT("/Script/") + Module + TEXT(".")))
            { bOwned = true; break; }
        }
        if (!bOwned) { Detail = TEXT("Reflected action class is not owned by this provider."); return false; }
        UClass* Class = FindObject<UClass>(nullptr, *Action.ReflectedClass);
        if (!Class) { Class = LoadObject<UClass>(nullptr, *Action.ReflectedClass); }
        UFunction* Function = Class ? Class->FindFunctionByName(FName(*FunctionName)) : nullptr;
        if (!Class || !Function || Function->GetOuter() != Class
            || !Function->HasAllFunctionFlags(FUNC_Static | FUNC_Native)
            || !Function->HasMetaData(RequiredMetadata))
        { Detail = TEXT("Provider action function must be native/static, owned by the provider, and explicitly annotated."); return false; }
        FStrProperty* Target = nullptr;
        FStrProperty* Expected = nullptr;
        FStrProperty* DetailProperty = nullptr;
        FBoolProperty* Return = nullptr;
        FBoolProperty* Complete = nullptr;
        FBoolProperty* Passed = nullptr;
        int32 Count = 0;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Prop = *It;
            if (!Prop->HasAnyPropertyFlags(CPF_Parm)) { continue; }
            ++Count;
            if (Prop->HasAnyPropertyFlags(CPF_ReturnParm)) { Return = CastField<FBoolProperty>(Prop); }
            else if (Prop->GetFName() == FName(TEXT("Target")) && !Prop->HasAnyPropertyFlags(CPF_OutParm))
            { Target = CastField<FStrProperty>(Prop); }
            else if (Prop->GetFName() == FName(TEXT("ExpectedValue")) && !Prop->HasAnyPropertyFlags(CPF_OutParm))
            { Expected = CastField<FStrProperty>(Prop); }
            else if (Prop->GetFName() == FName(TEXT("OutDetail")) && Prop->HasAnyPropertyFlags(CPF_OutParm))
            { DetailProperty = CastField<FStrProperty>(Prop); }
            else if (OutComplete && Prop->GetFName() == FName(TEXT("OutComplete")) && Prop->HasAnyPropertyFlags(CPF_OutParm))
            { Complete = CastField<FBoolProperty>(Prop); }
            else if (OutPassed && Prop->GetFName() == FName(TEXT("OutPassed")) && Prop->HasAnyPropertyFlags(CPF_OutParm))
            { Passed = CastField<FBoolProperty>(Prop); }
            else { Detail = TEXT("Unsupported action function parameter."); return false; }
        }
        const bool bIsPoll = OutComplete && OutPassed;
        if (Count != (bIsPoll ? 6 : 4) || !Target || !Expected || !DetailProperty || !Return
            || (bIsPoll && (!Complete || !Passed)))
        { Detail = TEXT("Action signature mismatch: expected documented native bool + string input/output contract."); return false; }
        if (bValidateOnly) { return true; }
        const int32 BufferSize = FMath::Max<int32>(Function->ParmsSize, Function->GetStructureSize());
        if (BufferSize <= 0 || BufferSize > 4096)
        { Detail = TEXT("Reflected action parameter buffer is out of bounds."); return false; }
        TArray<uint8> Params;
        Params.SetNumZeroed(BufferSize);
        Function->InitializeStruct(Params.GetData());
        *Target->ContainerPtrToValuePtr<FString>(Params.GetData()) = Step.Target;
        *Expected->ContainerPtrToValuePtr<FString>(Params.GetData()) = Step.ExpectedValue;
        Class->GetDefaultObject()->ProcessEvent(Function, Params.GetData());
        const bool bOk = Return->GetPropertyValue_InContainer(Params.GetData());
        Detail = *DetailProperty->ContainerPtrToValuePtr<FString>(Params.GetData());
        if (bIsPoll)
        {
            *OutComplete = Complete->GetPropertyValue_InContainer(Params.GetData());
            *OutPassed = Passed->GetPropertyValue_InContainer(Params.GetData());
        }
        Function->DestroyStruct(Params.GetData());
        return bOk;
    }

    Probe::EResultStatus EvaluateAlias(const FAssertionDefinition& Assertion,
        const FProbeTestStep& Step, FString& Reason)
    {
        const UEnum* Enum = StaticEnum<EProbeStepType>();
        const int64 Ordinal = Enum ? Enum->GetValueByNameString(Assertion.BuiltInType) : INDEX_NONE;
        if (Ordinal == INDEX_NONE) { Reason = TEXT("Missing built-in alias."); return Probe::EResultStatus::Error; }
        TStrongObjectPtr<UProbeTestAsset> Fixture(NewObject<UProbeTestAsset>(GetTransientPackage(), NAME_None, RF_Transient));
        Fixture->bNeedsConfiguration = false;
        FProbeTestStep Copy = Step;
        Copy.AssertionId = NAME_None;
        Copy.Type = static_cast<EProbeStepType>(Ordinal);
        if (Copy.Target.IsEmpty()) { Copy.Target = Assertion.DefaultTarget; }
        Fixture->Steps.Add(Copy);
        const Probe::FTestResult Result = Probe::RunTest(Fixture.Get());
        Reason = Result.Steps.IsEmpty() ? Result.Detail : Result.Steps[0].Detail;
        return Result.Status;
    }

    void UnregisterManaged()
    {
        for (const FName Id : RegisteredActionIds()) { Probe::UnregisterAssertion(Id); }
        for (const FName Id : RegisteredAssertionIds()) { Probe::UnregisterAssertion(Id); }
        for (const FName Id : RegisteredTemplateIds()) { Probe::UnregisterTemplate(Id); }
        RegisteredActionIds().Reset();
        RegisteredAssertionIds().Reset();
        RegisteredTemplateIds().Reset();
    }

    const FPortableTest* FindTest(const FString& Id)
    {
        for (const FPortableProvider& Provider : Cache())
        {
            for (const FPortableTest& Test : Provider.Tests)
            { if (Test.Id == Id) { return &Test; } }
        }
        return nullptr;
    }
    const FPortablePack* FindPack(const FString& Id)
    {
        for (const FPortableProvider& Provider : Cache())
        {
            for (const FPortablePack& Pack : Provider.Packs)
            { if (Pack.Id == Id) { return &Pack; } }
        }
        return nullptr;
    }

    struct FRunSession;
    TArray<TWeakPtr<FRunSession>>& ActiveSessions()
    { static TArray<TWeakPtr<FRunSession>> Sessions; return Sessions; }

    struct FRunSession : public TSharedFromThis<FRunSession>
    {
        TArray<FPortableTest> Queue;
        TArray<Probe::FTestResult> Results;
        TFunction<void(TArray<Probe::FTestResult>)> Finished;
        TStrongObjectPtr<UProbeTestAsset> Current;
        int32 Index = 0;
        bool bStopOnFailure = false;
        bool bAuthorizedActions = false;
        bool bCancelRequested = false;

        void RequestCancel()
        {
            bCancelRequested = true;
            if (Current.IsValid()) { Current->bPortableCancellationRequested = true; }
        }

        void Next()
        {
            const bool bStopped = bCancelRequested || (bStopOnFailure && !Results.IsEmpty()
                && (Results.Last().Status == Probe::EResultStatus::Failed
                    || Results.Last().Status == Probe::EResultStatus::Error));
            if (bStopped)
            {
                // Preserve explicit Skipped evidence for each pending member.
                while (Index < Queue.Num())
                {
                    Probe::FTestResult Skipped;
                    Skipped.Id = Queue[Index].Id;
                    Skipped.Name = Queue[Index].Name;
                    Skipped.AssetPath = TEXT("probe-json://") + Skipped.Id;
                    Skipped.Status = Probe::EResultStatus::Skipped;
                    Skipped.Detail = bCancelRequested
                        ? TEXT("Portable execution cancelled; member never started.")
                        : TEXT("Portable Pack stopped after prior test failure.");
                    Results.Add(MoveTemp(Skipped));
                    ++Index;
                }
            }
            if (Index >= Queue.Num())
            {
                ActiveSessions().RemoveAll([](const TWeakPtr<FRunSession>& Weak) { return !Weak.IsValid(); });
                ActiveSessions().RemoveAll([this](const TWeakPtr<FRunSession>& Weak) { return Weak.Pin().Get() == this; });
                --ActiveRuns();
                if (Finished) { Finished(MoveTemp(Results)); }
                return;
            }
            const FPortableTest Definition = Queue[Index++];
            FString Error;
            UProbeTestAsset* Fixture = nullptr;
            if (!MakeTransientTest(Definition, Fixture, Error, bAuthorizedActions))
            {
                Probe::FTestResult Failure;
                Failure.Name = Definition.Name;
                Failure.Id = Definition.Id;
                Failure.AssetPath = TEXT("probe-json://") + Definition.Id;
                Failure.Status = Probe::EResultStatus::Error;
                Failure.Detail = Error;
                Results.Add(MoveTemp(Failure));
                Next();
                return;
            }
            Current.Reset(Fixture);
            const TSharedRef<FRunSession> Self = AsShared();
            Probe::RunTestAsync(Current.Get(),
                [Self, Definition](Probe::FTestResult Result)
                {
                    // Use a stable definition ID, never the transient object's name.
                    Result.Id = Definition.Id;
                    Result.Name = Definition.Name;
                    Result.AssetPath = TEXT("probe-json://") + Definition.Id;
                    Self->Results.Add(MoveTemp(Result));
                    Self->Current.Reset();
                    Self->Next();
                });
        }
    };
}

void Refresh()
{
    check(IsInGameThread());
    if (ActiveRuns() > 0)
    {
        Issues().Add(TEXT("Refresh postponed: a portable Test/Pack is currently executing."));
        return;
    }
    UnregisterManaged();
    Cache().Reset(); Issues().Reset();
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
    {
        const FString Folder = Plugin->GetBaseDir() / TEXT("Resources/Probe");
        if (!IFileManager::Get().DirectoryExists(*Folder)) { continue; }
        if (!IFileManager::Get().FileExists(*(Folder / TEXT("manifest.json"))))
        {
            Issues().Add(Plugin->GetName() + TEXT(": Resources/Probe exists without manifest.json."));
            continue;
        }
        FPortableProvider Provider;
        FString Error;
        if (!LoadProvider(Folder, Plugin->GetName(), Provider, Error))
        { Issues().Add(Plugin->GetName() + TEXT(": ") + Error); continue; }
        for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
        { Provider.ModuleNames.Add(Module.Name.ToString()); }
        bool bInvalidReflectionOwner = false;
        for (const FAssertionDefinition& Declaration : Provider.Assertions)
        {
            if (Declaration.Kind != TEXT("reflected-readonly")) { continue; }
            bool bOwnsClass = false;
            for (const FString& ModuleName : Provider.ModuleNames)
            {
                if (Declaration.ReflectedClass.StartsWith(FString(TEXT("/Script/")) + ModuleName + TEXT(".")))
                { bOwnsClass = true; break; }
            }
            if (!bOwnsClass)
            {
                Issues().Add(Provider.PluginName + TEXT(": Reflected class is outside the plugin's declared modules: ")
                    + Declaration.ReflectedClass);
                bInvalidReflectionOwner = true;
            }
        }
        for (const FActionDefinition& Action : Provider.Actions)
        {
            if (Action.Kind == TEXT("native-fixture")) { continue; }
            bool bOwnsClass = false;
            for (const FString& ModuleName : Provider.ModuleNames)
            {
                if (Action.ReflectedClass.StartsWith(TEXT("/Script/") + ModuleName + TEXT(".")))
                { bOwnsClass = true; break; }
            }
            if (!bOwnsClass)
            {
                Issues().Add(Provider.PluginName + TEXT(": Reflected action class is outside its declared modules: ") + Action.ReflectedClass);
                bInvalidReflectionOwner = true;
            }
        }
        if (bInvalidReflectionOwner) { continue; }
        bool bCollision = false;
        for (const FAssertionDefinition& Assertion : Provider.Assertions)
        {
            if (Probe::FindExtensionAssertion(FName(*Assertion.Id)))
            { Issues().Add(Provider.PluginName + TEXT(": Existing assertion ID: ") + Assertion.Id); bCollision = true; }
        }
        for (const FActionDefinition& Action : Provider.Actions)
        {
            if (Action.Kind != TEXT("native-fixture") && Probe::FindExtensionAssertion(FName(*Action.Id)))
            { Issues().Add(Provider.PluginName + TEXT(": Existing action ID: ") + Action.Id); bCollision = true; }
        }
        for (const FPortableTest& Template : Provider.Templates)
        {
            if (Probe::FindExtensionTemplate(FName(*Template.Id)))
            { Issues().Add(Provider.PluginName + TEXT(": Existing template ID: ") + Template.Id); bCollision = true; }
        }
        if (bCollision) { continue; } // Never partially install a colliding provider.
        // Registration is managed by Probe, never called by the provider plugin.
        for (const FAssertionDefinition& Assertion : Provider.Assertions)
        {
            Probe::FExtensionAssertion Entry;
            Entry.Id = FName(*Assertion.Id);
            Entry.Name = Assertion.Name;
            Entry.Category = Assertion.Category;
            Entry.Description = Assertion.Description;
            Entry.Fields = Assertion.Kind == TEXT("builtin-alias")
                ? Probe::GetAssertionInfo(static_cast<EProbeStepType>(StaticEnum<EProbeStepType>()->GetValueByNameString(Assertion.BuiltInType))).Fields
                : (Probe::TargetField | Probe::ValueField);
            if (Assertion.Kind == TEXT("builtin-alias") && !Assertion.DefaultTarget.IsEmpty())
            {
                Entry.Fields = static_cast<uint8>(Entry.Fields & ~Probe::TargetField);
            }
            if (Assertion.Kind == TEXT("builtin-alias"))
            {
                Entry.Evaluate = [Assertion](const FProbeTestStep& Step, FString& Reason)
                { return EvaluateAlias(Assertion, Step, Reason); };
            }
            else
            {
                Entry.Evaluate = [Provider, Assertion](const FProbeTestStep& Step, FString& Reason)
                { return EvaluateReflected(Provider, Assertion, Step, Reason); };
            }
            if (Probe::RegisterAssertion(MoveTemp(Entry))) { RegisteredAssertionIds().Add(FName(*Assertion.Id)); }
            else { Issues().Add(Provider.PluginName + TEXT(": Assertion ID collision: ") + Assertion.Id); }
        }
        for (const FActionDefinition& Action : Provider.Actions)
        {
            if (Action.Kind == TEXT("native-fixture")) { continue; }
            Probe::FExtensionAssertion Entry;
            Entry.Id = FName(*Action.Id);
            Entry.Name = Action.Name;
            Entry.Category = Provider.ProviderId;
            Entry.Description = Action.Description;
            Entry.Fields = 0; // Parameters are optional; provider decides valid values.
            Entry.bRequiresPortableActionAuthorization = true;
            Entry.bPortableAsync = Action.Kind == TEXT("reflected-async");
            Entry.Evaluate = [Provider, Action](const FProbeTestStep& Step, FString& Detail)
            {
                // Fail closed BEFORE Begin. No provider may acquire state
                // without verified cleanup, and (for async) poll/cancel hooks.
                if (!CallAction(Provider, Action, Action.CleanupFunction,
                        TEXT("ProbeActionCleanup"), Step, nullptr, nullptr, Detail, true))
                { return Probe::EResultStatus::Error; }
                if (Action.Kind == TEXT("reflected-async"))
                {
                    bool bUnusedDone = false, bUnusedPassed = false;
                    if (!CallAction(Provider, Action, Action.PollFunction, TEXT("ProbeActionPoll"),
                            Step, &bUnusedDone, &bUnusedPassed, Detail, true)
                        || !CallAction(Provider, Action, Action.CancelFunction,
                            TEXT("ProbeActionCancel"), Step, nullptr, nullptr, Detail, true))
                    { return Probe::EResultStatus::Error; }
                }
                const bool bSucceeded = CallAction(Provider, Action, Action.StartFunction,
                    TEXT("ProbeActionStart"), Step, nullptr, nullptr, Detail);
                return bSucceeded ? Probe::EResultStatus::Passed : Probe::EResultStatus::Error;
            };
            Entry.Cleanup = [Provider, Action](const FProbeTestStep& Step, FString& Detail)
            { return CallAction(Provider, Action, Action.CleanupFunction,
                TEXT("ProbeActionCleanup"), Step, nullptr, nullptr, Detail); };
            if (Entry.bPortableAsync)
            {
                Entry.Poll = [Provider, Action](const FProbeTestStep& Step,
                    bool& OutComplete, bool& OutPassed, FString& Detail)
                { return CallAction(Provider, Action, Action.PollFunction,
                    TEXT("ProbeActionPoll"), Step, &OutComplete, &OutPassed, Detail); };
                Entry.Cancel = [Provider, Action](const FProbeTestStep& Step, FString& Detail)
                { return CallAction(Provider, Action, Action.CancelFunction,
                    TEXT("ProbeActionCancel"), Step, nullptr, nullptr, Detail); };
            }
            if (Probe::RegisterAssertion(MoveTemp(Entry))) { RegisteredActionIds().Add(FName(*Action.Id)); }
            else { Issues().Add(Provider.PluginName + TEXT(": Action registration collision: ") + Action.Id); }
        }
        for (const FPortableTest& Template : Provider.Templates)
        {
            Probe::FExtensionTemplate Entry;
            Entry.Id = FName(*Template.Id);
            Entry.Name = Template.Name;
            Entry.Category = Provider.ProviderId;
            Entry.Description = Template.Description;
            Entry.Steps = Template.Steps;
            Entry.bRequiresConfiguration = true;
            if (Probe::RegisterTemplate(MoveTemp(Entry))) { RegisteredTemplateIds().Add(FName(*Template.Id)); }
            else { Issues().Add(Provider.PluginName + TEXT(": Template ID collision: ") + Template.Id); }
        }
        Cache().Add(MoveTemp(Provider));
    }
}

void Clear()
{
    check(IsInGameThread());
    UnregisterManaged();
    Cache().Reset();
    Issues().Reset();
}

const TArray<FPortableProvider>& Providers() { return Cache(); }
const TArray<FString>& Diagnostics() { return Issues(); }

FString DiscoverySummary()
{
    int32 Tests = 0, Packs = 0, Actions = 0;
    for (const FPortableProvider& Provider : Cache())
    { Tests += Provider.Tests.Num(); Packs += Provider.Packs.Num(); Actions += Provider.Actions.Num(); }
    return FString::Printf(TEXT("%d providers, %d tests, %d packs, %d actions, %d diagnostics."),
        Cache().Num(), Tests, Packs, Actions, Issues().Num());
}

bool RequiresAuthorization(const FString& Id, bool bPack)
{
    if (!bPack)
    {
        const FPortableTest* Test = FindTest(Id);
        return Test && Test->bRequiresActionAuthorization;
    }
    const FPortablePack* Pack = FindPack(Id);
    if (!Pack) { return false; }
    for (const FString& TestId : Pack->TestIds)
    {
        const FPortableTest* Test = FindTest(TestId);
        if (Test && Test->bRequiresActionAuthorization) { return true; }
    }
    return false;
}

bool RequestCancel()
{
    check(IsInGameThread());
    bool bRequested = false;
    for (const TWeakPtr<FRunSession>& Weak : ActiveSessions())
    {
        if (const TSharedPtr<FRunSession> Session = Weak.Pin())
        { Session->RequestCancel(); bRequested = true; }
    }
    return bRequested;
}

bool MakeTransientTest(const FPortableTest& Definition, UProbeTestAsset*& Out, FString& Reason, bool bAuthorizeActions)
{
    check(IsInGameThread()); Out = nullptr; Reason.Reset();
    if (Definition.bRequiresActionAuthorization && !bAuthorizeActions)
    { Reason = TEXT("Portable Test declares state-changing actions. Explicit RunTestActions/RunPackActions authorization is required."); return false; }
    if (Definition.Steps.IsEmpty() || Definition.Steps.Num() > 256)
    { Reason = TEXT("Portable Test has invalid step count."); return false; }
    for (const FProbeTestStep& Step : Definition.Steps)
    {
        if (!Step.AssertionId.IsNone() && RegisteredActionIds().Contains(Step.AssertionId)
            && !Definition.bRequiresActionAuthorization)
        { Reason = TEXT("Refusing undeclared action permission in manually constructed portable Test."); return false; }
        if (Step.AssertionId.IsNone() && IsFixtureNativeType(Step.Type)
            && !Definition.bRequiresActionAuthorization)
        { Reason = TEXT("Refusing fixture operation without a declared action selector."); return false; }
        if ((!Step.AssertionId.IsNone() && !RegisteredAssertionIds().Contains(Step.AssertionId)
                && !(bAuthorizeActions && RegisteredActionIds().Contains(Step.AssertionId)))
            || (Step.AssertionId.IsNone() && !IsReadOnlyNativeType(Step.Type)
                && !(bAuthorizeActions && IsFixtureNativeType(Step.Type))))
        {
            Reason = TEXT("Portable Test step is not an authorized read-only assertion or allow-listed provider fixture/action.");
            return false;
        }
    }
    UProbeTestAsset* Test = NewObject<UProbeTestAsset>(GetTransientPackage(), NAME_None, RF_Transient);
    Test->TestDescription = Definition.Description;
    Test->Steps = Definition.Steps;
    Test->bEnabled = Definition.bEnabled;
    Test->bNeedsConfiguration = false; // Explicit manual run; never included in Run All.
    Test->bAllowEditorUIInput = false;
    Test->bAllowTransientEditorActors = bAuthorizeActions && Definition.bRequiresTransientActors;
    Test->bPortableActionsAuthorized = bAuthorizeActions && Definition.bRequiresActionAuthorization;
    Out = Test;
    return true;
}

bool RunTest(const FString& TestId, TFunction<void(TArray<Probe::FTestResult>)> OnComplete, FString& Reason,
    bool bAuthorizeActions)
{
    check(IsInGameThread()); Reason.Reset();
    const FPortableTest* Test = FindTest(TestId);
    if (!Test) { Reason = TEXT("Unknown portable Test ID: ") + TestId; return false; }
    if (Test->bRequiresActionAuthorization && !bAuthorizeActions)
    { Reason = TEXT("State-changing Test requires explicit authorization. Use Probe.Portable.RunTestActions."); return false; }
    const TSharedRef<FRunSession> Session = MakeShared<FRunSession>();
    Session->bAuthorizedActions = bAuthorizeActions;
    Session->Queue.Add(*Test);
    Session->Finished = MoveTemp(OnComplete);
    ++ActiveRuns();
    ActiveSessions().Add(Session);
    Session->Next();
    return true;
}

bool RunPack(const FString& PackId, TFunction<void(TArray<Probe::FTestResult>)> OnComplete, FString& Reason,
    bool bAuthorizeActions)
{
    check(IsInGameThread()); Reason.Reset();
    const FPortablePack* Pack = FindPack(PackId);
    if (!Pack) { Reason = TEXT("Unknown portable Pack ID: ") + PackId; return false; }
    if (RequiresAuthorization(PackId, true) && !bAuthorizeActions)
    { Reason = TEXT("Pack includes state-changing Tests. Use Probe.Portable.RunPackActions to authorize."); return false; }
    const TSharedRef<FRunSession> Session = MakeShared<FRunSession>();
    Session->bAuthorizedActions = bAuthorizeActions;
    Session->bStopOnFailure = Pack->bStopOnFailure;
    for (const FString& Id : Pack->TestIds)
    {
        const FPortableTest* Definition = FindTest(Id);
        if (!Definition) { Reason = TEXT("Portable Pack references unavailable test: ") + Id; return false; }
        Session->Queue.Add(*Definition);
    }
    Session->Finished = MoveTemp(OnComplete);
    ++ActiveRuns();
    ActiveSessions().Add(Session);
    Session->Next();
    return true;
}
}
