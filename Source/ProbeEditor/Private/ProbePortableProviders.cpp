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
    int32& ActiveRuns() { static int32 Count = 0; return Count; }

    bool IsReadOnlyNativeType(EProbeStepType Type)
    {
        const int32 Index = static_cast<int32>(Type);
        return !((Index >= static_cast<int32>(EProbeStepType::FixtureCreateObject)
                   && Index <= static_cast<int32>(EProbeStepType::FixtureDestroyObject))
            || (Index >= static_cast<int32>(EProbeStepType::SelectionClear)
                   && Index <= static_cast<int32>(EProbeStepType::ActorFixtureScaleNotNearlyEquals))
            || Type == EProbeStepType::UIWidgetClick || Type == EProbeStepType::UIWidgetTypeText
            || Type == EProbeStepType::UIWidgetClearText || Type == EProbeStepType::UIWidgetAppendText);
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
        if (!OptionalString(Object, TEXT("assertion"), Assertion, Reason)
            || !OptionalString(Object, TEXT("type"), Type, Reason)) { return false; }
        if (Assertion.IsEmpty() == Type.IsEmpty())
        { Reason = TEXT("Each step needs exactly one of 'type' or 'assertion'."); return false; }
        if (!Assertion.IsEmpty()) { Step.AssertionId = FName(*Assertion); }
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
        return ParseSteps(Object, Out.Steps, Reason);
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
    TArray<FString> Assertions, Templates, Tests, Packs;
    if (!Paths(Manifest, TEXT("assertions"), Assertions, Reason)
        || !Paths(Manifest, TEXT("templates"), Templates, Reason)
        || !Paths(Manifest, TEXT("tests"), Tests, Reason)
        || !Paths(Manifest, TEXT("packs"), Packs, Reason)) { return false; }
    TSet<FString> AllPaths;
    for (const TArray<FString>* List : {&Assertions, &Templates, &Tests, &Packs})
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
            else if (FCString::Strcmp(Kind, TEXT("pack")) == 0)
            {
                FPortablePack Definition;
                if (!ParsePack(Object, PluginName, Definition, Reason)) { return false; }
                Id = Definition.Id;
                OutProvider.Packs.Add(MoveTemp(Definition));
            }
            else
            {
                FPortableTest Definition;
                if (!ParseTest(Object, PluginName,
                    FCString::Strcmp(Kind, TEXT("test")) == 0 ? TEXT("ProbeTest/1") : TEXT("ProbeTemplate/1"), Definition, Reason))
                { return false; }
                Id = Definition.Id;
                (FCString::Strcmp(Kind, TEXT("test")) == 0 ? OutProvider.Tests : OutProvider.Templates).Add(MoveTemp(Definition));
            }
            const FString CaseFoldedId = Id.ToLower();
            if (AllIds.Contains(CaseFoldedId)) { Reason = TEXT("Duplicate provider definition ID: ") + Id; return false; }
            AllIds.Add(CaseFoldedId);
        }
        return true;
    };
    if (!LoadFiles(Assertions, TEXT("assertion")) || !LoadFiles(Templates, TEXT("template"))
        || !LoadFiles(Tests, TEXT("test")) || !LoadFiles(Packs, TEXT("pack"))) { return false; }
    // No orphan/cross-provider references: all dependencies resolve within this plugin.
    TSet<FString> AssertionIds, TestIds;
    for (const FAssertionDefinition& Assertion : OutProvider.Assertions) { AssertionIds.Add(Assertion.Id); }
    for (const FPortableTest& Test : OutProvider.Tests) { TestIds.Add(Test.Id); }
    for (const FPortablePack& Pack : OutProvider.Packs)
    {
        for (const FString& TestId : Pack.TestIds)
        {
            if (!TestIds.Contains(TestId)) { Reason = TEXT("Pack references an undefined Test: ") + TestId; return false; }
        }
    }
    for (const TArray<FPortableTest>* List : {&OutProvider.Tests, &OutProvider.Templates})
    {
        for (const FPortableTest& Test : *List)
        {
            for (const FProbeTestStep& Step : Test.Steps)
            {
                if (!Step.AssertionId.IsNone() && !AssertionIds.Contains(Step.AssertionId.ToString()))
                { Reason = TEXT("Test references an undeclared provider assertion: ") + Step.AssertionId.ToString(); return false; }
            }
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
        for (const FName Id : RegisteredAssertionIds()) { Probe::UnregisterAssertion(Id); }
        for (const FName Id : RegisteredTemplateIds()) { Probe::UnregisterTemplate(Id); }
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

    struct FRunSession : public TSharedFromThis<FRunSession>
    {
        TArray<FPortableTest> Queue;
        TArray<Probe::FTestResult> Results;
        TFunction<void(TArray<Probe::FTestResult>)> Finished;
        TStrongObjectPtr<UProbeTestAsset> Current;
        int32 Index = 0;
        bool bStopOnFailure = false;

        void Next()
        {
            const bool bStopped = bStopOnFailure && !Results.IsEmpty()
                && (Results.Last().Status == Probe::EResultStatus::Failed
                    || Results.Last().Status == Probe::EResultStatus::Error);
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
                    Skipped.Detail = TEXT("Portable Pack stopped after prior test failure.");
                    Results.Add(MoveTemp(Skipped));
                    ++Index;
                }
            }
            if (Index >= Queue.Num())
            {
                --ActiveRuns();
                if (Finished) { Finished(MoveTemp(Results)); }
                return;
            }
            const FPortableTest Definition = Queue[Index++];
            FString Error;
            UProbeTestAsset* Fixture = nullptr;
            if (!MakeTransientTest(Definition, Fixture, Error))
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
        if (bInvalidReflectionOwner) { continue; }
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

bool MakeTransientTest(const FPortableTest& Definition, UProbeTestAsset*& Out, FString& Reason)
{
    check(IsInGameThread()); Out = nullptr; Reason.Reset();
    if (Definition.Steps.IsEmpty() || Definition.Steps.Num() > 256)
    { Reason = TEXT("Portable Test has invalid step count."); return false; }
    for (const FProbeTestStep& Step : Definition.Steps)
    {
        if ((!Step.AssertionId.IsNone() && !RegisteredAssertionIds().Contains(Step.AssertionId))
            || (Step.AssertionId.IsNone() && !IsReadOnlyNativeType(Step.Type)))
        {
            Reason = TEXT("A portable Test can only use read-only native assertions or managed JSON provider assertions.");
            return false;
        }
    }
    UProbeTestAsset* Test = NewObject<UProbeTestAsset>(GetTransientPackage(), NAME_None, RF_Transient);
    Test->TestDescription = Definition.Description;
    Test->Steps = Definition.Steps;
    Test->bEnabled = Definition.bEnabled;
    Test->bNeedsConfiguration = false; // Explicit manual run; never included in Run All.
    Test->bAllowEditorUIInput = false;
    Test->bAllowTransientEditorActors = false;
    Out = Test;
    return true;
}

bool RunTest(const FString& TestId, TFunction<void(TArray<Probe::FTestResult>)> OnComplete, FString& Reason)
{
    check(IsInGameThread()); Reason.Reset();
    const FPortableTest* Test = FindTest(TestId);
    if (!Test) { Reason = TEXT("Unknown portable Test ID: ") + TestId; return false; }
    const TSharedRef<FRunSession> Session = MakeShared<FRunSession>();
    Session->Queue.Add(*Test);
    Session->Finished = MoveTemp(OnComplete);
    ++ActiveRuns();
    Session->Next();
    return true;
}

bool RunPack(const FString& PackId, TFunction<void(TArray<Probe::FTestResult>)> OnComplete, FString& Reason)
{
    check(IsInGameThread()); Reason.Reset();
    const FPortablePack* Pack = FindPack(PackId);
    if (!Pack) { Reason = TEXT("Unknown portable Pack ID: ") + PackId; return false; }
    const TSharedRef<FRunSession> Session = MakeShared<FRunSession>();
    Session->bStopOnFailure = Pack->bStopOnFailure;
    for (const FString& Id : Pack->TestIds)
    {
        const FPortableTest* Definition = FindTest(Id);
        if (!Definition) { Reason = TEXT("Portable Pack references unavailable test: ") + Id; return false; }
        Session->Queue.Add(*Definition);
    }
    Session->Finished = MoveTemp(OnComplete);
    ++ActiveRuns();
    Session->Next();
    return true;
}
}
