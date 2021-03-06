#include "calltarget_tokens.h"

#include "dd_profiler_constants.h"
#include "il_rewriter_wrapper.h"
#include "logging.h"
#include "module_metadata.h"

namespace trace {

/**
 * PRIVATE
 **/

HRESULT CallTargetTokens::EnsureCorLibTokens() {
  ModuleMetadata* module_metadata = GetMetadata();
  AssemblyProperty corAssemblyProperty = *module_metadata->corAssemblyProperty;

  // *** Ensure corlib assembly ref
  if (corLibAssemblyRef == mdAssemblyRefNil) {
    auto hr = module_metadata->assembly_emit->DefineAssemblyRef(
        corAssemblyProperty.ppbPublicKey, corAssemblyProperty.pcbPublicKey,
        corAssemblyProperty.szName.data(), &corAssemblyProperty.pMetaData,
        &corAssemblyProperty.pulHashAlgId,
        sizeof(corAssemblyProperty.pulHashAlgId),
        corAssemblyProperty.assemblyFlags, &corLibAssemblyRef);
    if (corLibAssemblyRef == mdAssemblyRefNil) {
      Warn("Wrapper corLibAssemblyRef could not be defined.");
      return hr;
    }
  }

  // *** Ensure System.Object type ref
  if (objectTypeRef == mdTypeRefNil) {
    auto hr = module_metadata->metadata_emit->DefineTypeRefByName(
        corLibAssemblyRef, SystemObject.data(), &objectTypeRef);
    if (FAILED(hr)) {
      Warn("Wrapper objectTypeRef could not be defined.");
      return hr;
    }
  }

  // *** Ensure System.Exception type ref
  if (exTypeRef == mdTypeRefNil) {
    auto hr = module_metadata->metadata_emit->DefineTypeRefByName(
        corLibAssemblyRef, SystemException.data(), &exTypeRef);
    if (FAILED(hr)) {
      Warn("Wrapper exTypeRef could not be defined.");
      return hr;
    }
  }

  // *** Ensure System.Type type ref
  if (typeRef == mdTypeRefNil) {
    auto hr = module_metadata->metadata_emit->DefineTypeRefByName(
        corLibAssemblyRef, SystemTypeName.data(), &typeRef);
    if (FAILED(hr)) {
      Warn("Wrapper typeRef could not be defined.");
      return hr;
    }
  }

  // *** Ensure System.RuntimeTypeHandle type ref
  if (runtimeTypeHandleRef == mdTypeRefNil) {
    auto hr = module_metadata->metadata_emit->DefineTypeRefByName(
        corLibAssemblyRef, RuntimeTypeHandleTypeName.data(),
        &runtimeTypeHandleRef);
    if (FAILED(hr)) {
      Warn("Wrapper runtimeTypeHandleRef could not be defined.");
      return hr;
    }
  }

  // *** Ensure Type.GetTypeFromHandle token
  if (getTypeFromHandleToken == mdTokenNil) {
    unsigned runtimeTypeHandle_buffer;
    auto runtimeTypeHandle_size =
        CorSigCompressToken(runtimeTypeHandleRef, &runtimeTypeHandle_buffer);

    unsigned type_buffer;
    auto type_size = CorSigCompressToken(typeRef, &type_buffer);

    auto* signature = new COR_SIGNATURE[runtimeTypeHandle_size + type_size + 4];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_DEFAULT;
    signature[offset++] = 0x01;
    signature[offset++] = ELEMENT_TYPE_CLASS;
    memcpy(&signature[offset], &type_buffer, type_size);
    offset += type_size;
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &runtimeTypeHandle_buffer,
           runtimeTypeHandle_size);
    offset += runtimeTypeHandle_size;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        typeRef, GetTypeFromHandleMethodName.data(), signature,
        sizeof(signature), &getTypeFromHandleToken);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper getTypeFromHandleToken could not be defined.");
      return hr;
    }
  }

  // *** Ensure System.RuntimeMethodHandle type ref
  if (runtimeMethodHandleRef == mdTypeRefNil) {
    auto hr = module_metadata->metadata_emit->DefineTypeRefByName(
        corLibAssemblyRef, RuntimeMethodHandleTypeName.data(),
        &runtimeMethodHandleRef);
    if (FAILED(hr)) {
      Warn("Wrapper runtimeMethodHandleRef could not be defined.");
      return hr;
    }
  }

  return S_OK;
}

HRESULT CallTargetTokens::EnsureBaseCalltargetTokens() {
  auto hr = EnsureCorLibTokens();
  if (FAILED(hr)) {
    return hr;
  }

  ModuleMetadata* module_metadata = GetMetadata();

  // *** Ensure profiler assembly ref
  if (profilerAssemblyRef == mdAssemblyRefNil) {
    const AssemblyReference assemblyReference =
        managed_profiler_full_assembly_version;
    ASSEMBLYMETADATA assembly_metadata{};

    assembly_metadata.usMajorVersion = assemblyReference.version.major;
    assembly_metadata.usMinorVersion = assemblyReference.version.minor;
    assembly_metadata.usBuildNumber = assemblyReference.version.build;
    assembly_metadata.usRevisionNumber = assemblyReference.version.revision;
    if (assemblyReference.locale == "neutral"_W) {
      assembly_metadata.szLocale = const_cast<WCHAR*>("\0"_W.c_str());
      assembly_metadata.cbLocale = 0;
    } else {
      assembly_metadata.szLocale =
          const_cast<WCHAR*>(assemblyReference.locale.c_str());
      assembly_metadata.cbLocale = (DWORD)(assemblyReference.locale.size());
    }

    DWORD public_key_size = 8;
    if (assemblyReference.public_key == trace::PublicKey()) {
      public_key_size = 0;
    }

    hr = module_metadata->assembly_emit->DefineAssemblyRef(
        &assemblyReference.public_key.data, public_key_size,
        assemblyReference.name.data(), &assembly_metadata, NULL, 0, 0,
        &profilerAssemblyRef);

    if (FAILED(hr)) {
      Warn("Wrapper profilerAssemblyRef could not be defined.");
      return hr;
    }
  }

  // *** Ensure calltarget type ref
  if (callTargetTypeRef == mdTypeRefNil) {
    hr = module_metadata->metadata_emit->DefineTypeRefByName(
        profilerAssemblyRef, managed_profiler_calltarget_type.data(),
        &callTargetTypeRef);
    if (FAILED(hr)) {
      Warn("Wrapper callTargetTypeRef could not be defined.");
      return hr;
    }
  }

  // *** Ensure calltargetstate type ref
  if (callTargetStateTypeRef == mdTypeRefNil) {
    hr = module_metadata->metadata_emit->DefineTypeRefByName(
        profilerAssemblyRef, managed_profiler_calltarget_statetype.data(),
        &callTargetStateTypeRef);
    if (FAILED(hr)) {
      Warn("Wrapper callTargetStateTypeRef could not be defined.");
      return hr;
    }
  }

  // *** Ensure CallTargetState.GetDefault() member ref
  if (callTargetStateTypeGetDefault == mdMemberRefNil) {
    unsigned callTargetStateTypeBuffer;
    auto callTargetStateTypeSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateTypeBuffer);

    auto signatureLength = 3 + callTargetStateTypeSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_DEFAULT;
    signature[offset++] = 0x00;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateTypeBuffer,
           callTargetStateTypeSize);
    offset += callTargetStateTypeSize;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetStateTypeRef,
        managed_profiler_calltarget_statetype_getdefault_name.data(), signature,
        signatureLength, &callTargetStateTypeGetDefault);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper callTargetStateTypeGetDefault could not be defined.");
      return hr;
    }
  }

  return S_OK;
}

mdTypeRef CallTargetTokens::GetTargetStateTypeRef() {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return mdTypeRefNil;
  }
  return callTargetStateTypeRef;
}

mdTypeRef CallTargetTokens::GetTargetVoidReturnTypeRef() {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return mdTypeRefNil;
  }

  ModuleMetadata* module_metadata = GetMetadata();

  // *** Ensure calltargetreturn void type ref
  if (callTargetReturnVoidTypeRef == mdTypeRefNil) {
    hr = module_metadata->metadata_emit->DefineTypeRefByName(
        profilerAssemblyRef, managed_profiler_calltarget_returntype.data(),
        &callTargetReturnVoidTypeRef);
    if (FAILED(hr)) {
      Warn("Wrapper callTargetReturnVoidTypeRef could not be defined.");
      return mdTypeRefNil;
    }
  }

  return callTargetReturnVoidTypeRef;
}

mdTypeSpec CallTargetTokens::GetTargetReturnValueTypeRef(
    FunctionMethodArgument* returnArgument) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return mdTypeSpecNil;
  }

  ModuleMetadata* module_metadata = GetMetadata();
  mdTypeSpec returnValueTypeSpec = mdTypeSpecNil;

  // *** Ensure calltargetreturn type ref
  if (callTargetReturnTypeRef == mdTypeRefNil) {
    hr = module_metadata->metadata_emit->DefineTypeRefByName(
        profilerAssemblyRef,
        managed_profiler_calltarget_returntype_generics.data(),
        &callTargetReturnTypeRef);
    if (FAILED(hr)) {
      Warn("Wrapper callTargetReturnTypeRef could not be defined.");
      return mdTypeSpecNil;
    }
  }

  PCCOR_SIGNATURE returnSignatureBuffer;
  auto returnSignatureLength =
      returnArgument->GetSignature(returnSignatureBuffer);

  // Get The base calltargetReturnTypeRef Buffer and Size
  unsigned callTargetReturnTypeRefBuffer;
  auto callTargetReturnTypeRefSize = CorSigCompressToken(
      callTargetReturnTypeRef, &callTargetReturnTypeRefBuffer);

  auto signatureLength =
      3 + callTargetReturnTypeRefSize + returnSignatureLength;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = ELEMENT_TYPE_GENERICINST;
  signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  memcpy(&signature[offset], &callTargetReturnTypeRefBuffer,
         callTargetReturnTypeRefSize);
  offset += callTargetReturnTypeRefSize;
  signature[offset++] = 0x01;
  memcpy(&signature[offset], returnSignatureBuffer, returnSignatureLength);
  offset += returnSignatureLength;

  hr = module_metadata->metadata_emit->GetTokenFromTypeSpec(
      signature, signatureLength, &returnValueTypeSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating return value type spec");
    return mdTypeSpecNil;
  }

  return returnValueTypeSpec;
}

mdMemberRef CallTargetTokens::GetCallTargetStateDefaultMemberRef() {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return mdMemberRefNil;
  }
  return callTargetStateTypeGetDefault;
}

mdMemberRef CallTargetTokens::GetCallTargetReturnVoidDefaultMemberRef() {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return mdMemberRefNil;
  }

  // *** Ensure CallTargetReturn.GetDefault() member ref
  if (callTargetReturnVoidTypeGetDefault == mdMemberRefNil) {
    ModuleMetadata* module_metadata = GetMetadata();

    unsigned callTargetReturnVoidTypeBuffer;
    auto callTargetReturnVoidTypeSize = CorSigCompressToken(
        callTargetReturnVoidTypeRef, &callTargetReturnVoidTypeBuffer);

    auto signatureLength = 3 + callTargetReturnVoidTypeSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_DEFAULT;
    signature[offset++] = 0x00;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetReturnVoidTypeBuffer,
           callTargetReturnVoidTypeSize);
    offset += callTargetReturnVoidTypeSize;

    hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetReturnVoidTypeRef,
        managed_profiler_calltarget_returntype_getdefault_name.data(),
        signature, signatureLength, &callTargetReturnVoidTypeGetDefault);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper callTargetReturnVoidTypeGetDefault could not be defined.");
      return mdMemberRefNil;
    }
  }

  return callTargetReturnVoidTypeGetDefault;
}

mdMemberRef CallTargetTokens::GetCallTargetReturnValueDefaultMemberRef(
    mdTypeSpec callTargetReturnTypeSpec) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return mdMemberRefNil;
  }
  if (callTargetReturnTypeRef == mdTypeRefNil) {
    Warn("Wrapper callTargetReturnTypeGetDefault could not be defined because callTargetReturnTypeRef is null.");
    return mdMemberRefNil;
  }

  mdMemberRef callTargetReturnTypeGetDefault = mdMemberRefNil;

  // *** Ensure CallTargetReturn<T>.GetDefault() member ref
  ModuleMetadata* module_metadata = GetMetadata();

  unsigned callTargetReturnTypeRefBuffer;
  auto callTargetReturnTypeRefSize = CorSigCompressToken(
      callTargetReturnTypeRef, &callTargetReturnTypeRefBuffer);

  auto signatureLength = 7 + callTargetReturnTypeRefSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_DEFAULT;
  signature[offset++] = 0x00;
  signature[offset++] = ELEMENT_TYPE_GENERICINST;
  signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  memcpy(&signature[offset], &callTargetReturnTypeRefBuffer,
         callTargetReturnTypeRefSize);
  offset += callTargetReturnTypeRefSize;
  signature[offset++] = 0x01;
  signature[offset++] = ELEMENT_TYPE_VAR;
  signature[offset++] = 0x00;

  hr = module_metadata->metadata_emit->DefineMemberRef(
      callTargetReturnTypeSpec,
      managed_profiler_calltarget_returntype_getdefault_name.data(), signature,
      signatureLength, &callTargetReturnTypeGetDefault);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Wrapper callTargetReturnTypeGetDefault could not be defined.");
    return mdMemberRefNil;
  }

  return callTargetReturnTypeGetDefault;
}

mdMethodSpec CallTargetTokens::GetCallTargetDefaultValueMethodSpec(
    FunctionMethodArgument* methodArgument) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return mdMethodSpecNil;
  }

  mdMethodSpec getDefaultMethodSpec = mdMethodSpecNil;
  ModuleMetadata* module_metadata = GetMetadata();

  // *** Ensure we have the CallTargetInvoker.GetDefaultValue<> memberRef
  if (getDefaultMemberRef == mdMemberRefNil) {
    auto signatureLength = 5;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x01;
    signature[offset++] = 0x00;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x00;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef,
        managed_profiler_calltarget_getdefaultvalue_name.data(), signature,
        signatureLength, &getDefaultMemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper getDefaultMemberRef could not be defined.");
      return hr;
    }
  }

  // *** Create de MethodSpec using the FunctionMethodArgument

  // Gets the Return type signature
  PCCOR_SIGNATURE methodArgumentSignature = nullptr;
  ULONG methodArgumentSignatureSize;
  methodArgumentSignatureSize =
      methodArgument->GetSignature(methodArgumentSignature);

  auto signatureLength = 2 + methodArgumentSignatureSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;
  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x01;

  memcpy(&signature[offset], methodArgumentSignature,
         methodArgumentSignatureSize);
  offset += methodArgumentSignatureSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      getDefaultMemberRef, signature, signatureLength, &getDefaultMethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating getDefaultMethodSpec.");
    return mdMethodSpecNil;
  }

  return getDefaultMethodSpec;
}

mdToken CallTargetTokens::GetCurrentTypeRef(const TypeInfo* currentType, bool& isValueType) {
  if (currentType->type_spec != mdTypeSpecNil) {
    return currentType->type_spec;
  } else if (!currentType->isGeneric) {
    return currentType->id;
  } else {
    isValueType = false;
    return objectTypeRef;
  }
}

HRESULT CallTargetTokens::ModifyLocalSig(
    ILRewriter* reWriter, FunctionMethodArgument* methodReturnValue,
    ULONG* callTargetStateIndex, ULONG* exceptionIndex,
    ULONG* callTargetReturnIndex, ULONG* returnValueIndex,
    mdToken* callTargetStateToken, mdToken* exceptionToken,
    mdToken* callTargetReturnToken) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }

  ModuleMetadata* module_metadata = GetMetadata();

  PCCOR_SIGNATURE originalSignature = nullptr;
  ULONG originalSignatureSize = 0;
  mdToken localVarSig = reWriter->GetTkLocalVarSig();

  if (localVarSig != mdTokenNil) {
    IfFailRet(module_metadata->metadata_import->GetSigFromToken(
        localVarSig, &originalSignature, &originalSignatureSize));

    // Check if the localvarsig has been already rewritten (the last local
    // should be the callTargetState)
    unsigned temp = 0;
    const auto len = CorSigCompressToken(callTargetStateTypeRef, &temp);
    if (originalSignatureSize - len > 0) {
      if (originalSignature[originalSignatureSize - len - 1] ==
          ELEMENT_TYPE_VALUETYPE) {
        if (memcmp(&originalSignature[originalSignatureSize - len], &temp,
                   len) == 0) {
          Warn("The signature for this method has been already modified.");
          return E_FAIL;
        }
      }
    }
  }

  ULONG newLocalsCount = 3;

  // Gets the calltarget state type buffer and size
  unsigned callTargetStateTypeRefBuffer;
  auto callTargetStateTypeRefSize = CorSigCompressToken(
      callTargetStateTypeRef, &callTargetStateTypeRefBuffer);

  // Gets the exception type buffer and size
  unsigned exTypeRefBuffer;
  auto exTypeRefSize = CorSigCompressToken(exTypeRef, &exTypeRefBuffer);

  // Gets the Return type signature
  PCCOR_SIGNATURE returnSignatureType = nullptr;
  ULONG returnSignatureTypeSize = 0;

  // Gets the CallTargetReturn<T> mdTypeSpec
  mdToken callTargetReturn = mdTokenNil;
  PCCOR_SIGNATURE callTargetReturnSignature = nullptr;
  ULONG callTargetReturnSignatureSize;
  unsigned callTargetReturnBuffer;
  ULONG callTargetReturnSize;
  ULONG callTargetReturnSizeForNewSignature = 0;
  unsigned retTypeElementType;
  auto retTypeFlags = methodReturnValue->GetTypeFlags(retTypeElementType);

  if (retTypeFlags != TypeFlagVoid) {
    returnSignatureTypeSize =
        methodReturnValue->GetSignature(returnSignatureType);
    callTargetReturn = GetTargetReturnValueTypeRef(methodReturnValue);

    hr = module_metadata->metadata_import->GetTypeSpecFromToken(
        callTargetReturn, &callTargetReturnSignature,
        &callTargetReturnSignatureSize);
    if (FAILED(hr)) {
      return E_FAIL;
    }

    callTargetReturnSizeForNewSignature = callTargetReturnSignatureSize;

    newLocalsCount++;
  } else {
    callTargetReturn = GetTargetVoidReturnTypeRef();
    callTargetReturnSize =
        CorSigCompressToken(callTargetReturn, &callTargetReturnBuffer);
    callTargetReturnSizeForNewSignature = 1 + callTargetReturnSize;
  }

  // New signature size
  ULONG newSignatureSize =
      originalSignatureSize + returnSignatureTypeSize + (1 + exTypeRefSize) +
      callTargetReturnSizeForNewSignature + (1 + callTargetStateTypeRefSize);
  ULONG newSignatureOffset = 0;


  ULONG oldLocalsBuffer;
  ULONG oldLocalsLen = 0;
  unsigned newLocalsBuffer;
  ULONG newLocalsLen;

  // Calculate the new locals count
  if (originalSignatureSize == 0) {
    newSignatureSize += 2;
    newLocalsLen = CorSigCompressData(newLocalsCount, &newLocalsBuffer);
  } else {
    oldLocalsLen =
        CorSigUncompressData(originalSignature + 1, &oldLocalsBuffer);
    newLocalsCount += oldLocalsBuffer;
    newLocalsLen = CorSigCompressData(newLocalsCount, &newLocalsBuffer);
    newSignatureSize += newLocalsLen - oldLocalsLen;
  }

  // New signature declaration
  auto* newSignatureBuffer = new COR_SIGNATURE[newSignatureSize];
  newSignatureBuffer[newSignatureOffset++] = IMAGE_CEE_CS_CALLCONV_LOCAL_SIG;

  // Set the locals count
  memcpy(&newSignatureBuffer[newSignatureOffset], &newLocalsBuffer,
         newLocalsLen);
  newSignatureOffset += newLocalsLen;

  // Copy previous locals to the signature
  if (originalSignatureSize > 0) {
    const auto copyLength = originalSignatureSize - 1 - oldLocalsLen;
    memcpy(&newSignatureBuffer[newSignatureOffset],
           originalSignature + 1 + oldLocalsLen, copyLength);
    newSignatureOffset += copyLength;
  }

  // Add new locals

  // Return value local
  if (returnSignatureType != nullptr) {
    memcpy(&newSignatureBuffer[newSignatureOffset], returnSignatureType,
           returnSignatureTypeSize);
    newSignatureOffset += returnSignatureTypeSize;
  }

  // Exception value
  newSignatureBuffer[newSignatureOffset++] = ELEMENT_TYPE_CLASS;
  memcpy(&newSignatureBuffer[newSignatureOffset], &exTypeRefBuffer,
         exTypeRefSize);
  newSignatureOffset += exTypeRefSize;

  // CallTarget Return value
  if (callTargetReturnSignature != nullptr) {
    memcpy(&newSignatureBuffer[newSignatureOffset], callTargetReturnSignature,
           callTargetReturnSignatureSize);
    newSignatureOffset += callTargetReturnSignatureSize;
  } else {
    newSignatureBuffer[newSignatureOffset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&newSignatureBuffer[newSignatureOffset], &callTargetReturnBuffer,
           callTargetReturnSize);
    newSignatureOffset += callTargetReturnSize;
  }

  // CallTarget state value
  newSignatureBuffer[newSignatureOffset++] = ELEMENT_TYPE_VALUETYPE;
  memcpy(&newSignatureBuffer[newSignatureOffset], &callTargetStateTypeRefBuffer,
         callTargetStateTypeRefSize);
  newSignatureOffset += callTargetStateTypeRefSize;

  // Get new locals token
  mdToken newLocalVarSig;
  hr = module_metadata->metadata_emit->GetTokenFromSig(
      newSignatureBuffer, newSignatureSize, &newLocalVarSig);
  delete[] newSignatureBuffer;
  if (FAILED(hr)) {
    Warn("Error creating new locals var signature.");
    return hr;
  }

  reWriter->SetTkLocalVarSig(newLocalVarSig);
  *callTargetStateToken = callTargetStateTypeRef;
  *exceptionToken = exTypeRef;
  *callTargetReturnToken = callTargetReturn;
  if (returnSignatureType != nullptr) {
    *returnValueIndex = newLocalsCount - 4;
  } else {
    *returnValueIndex = static_cast<ULONG>(ULONG_MAX);
  }
  *exceptionIndex = newLocalsCount - 3;
  *callTargetReturnIndex = newLocalsCount - 2;
  *callTargetStateIndex = newLocalsCount - 1;
  return hr;
}

/**
 * PUBLIC
 **/

mdTypeRef CallTargetTokens::GetObjectTypeRef() { return objectTypeRef; }
mdTypeRef CallTargetTokens::GetExceptionTypeRef() { return exTypeRef; }
mdAssemblyRef CallTargetTokens::GetCorLibAssemblyRef() { return corLibAssemblyRef; }

HRESULT CallTargetTokens::ModifyLocalSigAndInitialize(
    void* rewriterWrapperPtr, FunctionInfo* functionInfo,
    ULONG* callTargetStateIndex, ULONG* exceptionIndex,
    ULONG* callTargetReturnIndex, ULONG* returnValueIndex,
    mdToken* callTargetStateToken, mdToken* exceptionToken,
    mdToken* callTargetReturnToken, ILInstr** firstInstruction) {
  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;

  // Modify the Local Var Signature of the method
  auto returnFunctionMethod = functionInfo->method_signature.GetRet();

  auto hr = ModifyLocalSig(rewriterWrapper->GetILRewriter(),
                           &returnFunctionMethod, callTargetStateIndex,
                           exceptionIndex, callTargetReturnIndex,
                           returnValueIndex, callTargetStateToken,
                           exceptionToken, callTargetReturnToken);

  if (FAILED(hr)) {
    Warn("ModifyLocalSig() failed.");
    return hr;
  }

  // Init locals
  if (*returnValueIndex != static_cast<ULONG>(ULONG_MAX)) {
    *firstInstruction = rewriterWrapper->CallMember(
        GetCallTargetDefaultValueMethodSpec(&returnFunctionMethod), false);
    rewriterWrapper->StLocal(*returnValueIndex);

    rewriterWrapper->CallMember(
        GetCallTargetReturnValueDefaultMemberRef(*callTargetReturnToken),
        false);
    rewriterWrapper->StLocal(*callTargetReturnIndex);
  } else {
    *firstInstruction = rewriterWrapper->CallMember(
        GetCallTargetReturnVoidDefaultMemberRef(), false);
    rewriterWrapper->StLocal(*callTargetReturnIndex);
  }
  rewriterWrapper->LoadNull();
  rewriterWrapper->StLocal(*exceptionIndex);
  // We don't need to initialize calltarget state because is going to be initialized right after this method call.
  // So we can save 2 instructions.
  /*rewriterWrapper->CallMember(GetCallTargetStateDefaultMemberRef(), false);
  rewriterWrapper->StLocal(*callTargetStateIndex);*/
  return S_OK;
}

HRESULT CallTargetTokens::WriteBeginMethodWithoutArguments(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }
  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (beginP0MemberRef == mdMemberRefNil) {
    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength = 6 + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x02;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_beginmethod_name.data(),
        signature, signatureLength, &beginP0MemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper beginP0MemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec beginP0MethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;
  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x02;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      beginP0MemberRef, signature, signatureLength, &beginP0MethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating begin method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(beginP0MethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteBeginMethodWithArguments(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, FunctionMethodArgument* arg1,
    ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }

  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (beginP1MemberRef == mdMemberRefNil) {
    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength = 8 + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x03;
    signature[offset++] = 0x02;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x02;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_beginmethod_name.data(),
        signature, signatureLength, &beginP1MemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper beginP1MemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec beginP1MethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  PCCOR_SIGNATURE arg1SignatureBuffer;
  auto arg1SignatureSize = arg1->GetSignature(arg1SignatureBuffer);

  auto signatureLength =
      4 + integrationTypeSize + currentTypeSize + arg1SignatureSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x03;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  memcpy(&signature[offset], arg1SignatureBuffer, arg1SignatureSize);
  offset += arg1SignatureSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      beginP1MemberRef, signature, signatureLength, &beginP1MethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating begin method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(beginP1MethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteBeginMethodWithArguments(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, FunctionMethodArgument* arg1,
    FunctionMethodArgument* arg2, ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }

  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (beginP2MemberRef == mdMemberRefNil) {
    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength = 10 + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x04;
    signature[offset++] = 0x03;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x02;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x03;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_beginmethod_name.data(),
        signature, signatureLength, &beginP2MemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper beginP2MemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec beginP2MethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  PCCOR_SIGNATURE arg1SignatureBuffer;
  auto arg1SignatureSize = arg1->GetSignature(arg1SignatureBuffer);

  PCCOR_SIGNATURE arg2SignatureBuffer;
  auto arg2SignatureSize = arg2->GetSignature(arg2SignatureBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize +
                         arg1SignatureSize + arg2SignatureSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x04;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  memcpy(&signature[offset], arg1SignatureBuffer, arg1SignatureSize);
  offset += arg1SignatureSize;

  memcpy(&signature[offset], arg2SignatureBuffer, arg2SignatureSize);
  offset += arg2SignatureSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      beginP2MemberRef, signature, signatureLength, &beginP2MethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating begin method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(beginP2MethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteBeginMethodWithArguments(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, FunctionMethodArgument* arg1,
    FunctionMethodArgument* arg2, FunctionMethodArgument* arg3,
    ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }

  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (beginP3MemberRef == mdMemberRefNil) {
    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength = 12 + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x05;
    signature[offset++] = 0x04;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x02;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x03;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x04;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_beginmethod_name.data(),
        signature, signatureLength, &beginP3MemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper beginP3MemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec beginP3MethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  PCCOR_SIGNATURE arg1SignatureBuffer;
  auto arg1SignatureSize = arg1->GetSignature(arg1SignatureBuffer);

  PCCOR_SIGNATURE arg2SignatureBuffer;
  auto arg2SignatureSize = arg2->GetSignature(arg2SignatureBuffer);

  PCCOR_SIGNATURE arg3SignatureBuffer;
  auto arg3SignatureSize = arg3->GetSignature(arg3SignatureBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize +
                         arg1SignatureSize + arg2SignatureSize +
                         arg3SignatureSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x05;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  memcpy(&signature[offset], arg1SignatureBuffer, arg1SignatureSize);
  offset += arg1SignatureSize;

  memcpy(&signature[offset], arg2SignatureBuffer, arg2SignatureSize);
  offset += arg2SignatureSize;

  memcpy(&signature[offset], arg3SignatureBuffer, arg3SignatureSize);
  offset += arg3SignatureSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      beginP3MemberRef, signature, signatureLength, &beginP3MethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating begin method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(beginP3MethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteBeginMethodWithArguments(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, FunctionMethodArgument* arg1,
    FunctionMethodArgument* arg2, FunctionMethodArgument* arg3,
    FunctionMethodArgument* arg4, ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }

  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (beginP4MemberRef == mdMemberRefNil) {
    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength = 14 + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x06;
    signature[offset++] = 0x05;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x02;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x03;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x04;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x05;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_beginmethod_name.data(),
        signature, signatureLength, &beginP4MemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper beginP4MemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec beginP4MethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  PCCOR_SIGNATURE arg1SignatureBuffer;
  auto arg1SignatureSize = arg1->GetSignature(arg1SignatureBuffer);

  PCCOR_SIGNATURE arg2SignatureBuffer;
  auto arg2SignatureSize = arg2->GetSignature(arg2SignatureBuffer);

  PCCOR_SIGNATURE arg3SignatureBuffer;
  auto arg3SignatureSize = arg3->GetSignature(arg3SignatureBuffer);

  PCCOR_SIGNATURE arg4SignatureBuffer;
  auto arg4SignatureSize = arg4->GetSignature(arg4SignatureBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize +
                         arg1SignatureSize + arg2SignatureSize +
                         arg3SignatureSize + arg4SignatureSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x06;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  memcpy(&signature[offset], arg1SignatureBuffer, arg1SignatureSize);
  offset += arg1SignatureSize;

  memcpy(&signature[offset], arg2SignatureBuffer, arg2SignatureSize);
  offset += arg2SignatureSize;

  memcpy(&signature[offset], arg3SignatureBuffer, arg3SignatureSize);
  offset += arg3SignatureSize;

  memcpy(&signature[offset], arg4SignatureBuffer, arg4SignatureSize);
  offset += arg4SignatureSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      beginP4MemberRef, signature, signatureLength, &beginP4MethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating begin method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(beginP4MethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteBeginMethodWithArguments(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, FunctionMethodArgument* arg1,
    FunctionMethodArgument* arg2, FunctionMethodArgument* arg3,
    FunctionMethodArgument* arg4, FunctionMethodArgument* arg5,
    ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }

  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (beginP5MemberRef == mdMemberRefNil) {
    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength = 16 + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x07;
    signature[offset++] = 0x06;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x02;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x03;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x04;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x05;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x06;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_beginmethod_name.data(),
        signature, signatureLength, &beginP5MemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper beginP5MemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec beginP5MethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  PCCOR_SIGNATURE arg1SignatureBuffer;
  auto arg1SignatureSize = arg1->GetSignature(arg1SignatureBuffer);

  PCCOR_SIGNATURE arg2SignatureBuffer;
  auto arg2SignatureSize = arg2->GetSignature(arg2SignatureBuffer);

  PCCOR_SIGNATURE arg3SignatureBuffer;
  auto arg3SignatureSize = arg3->GetSignature(arg3SignatureBuffer);

  PCCOR_SIGNATURE arg4SignatureBuffer;
  auto arg4SignatureSize = arg4->GetSignature(arg4SignatureBuffer);

  PCCOR_SIGNATURE arg5SignatureBuffer;
  auto arg5SignatureSize = arg5->GetSignature(arg5SignatureBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize +
                         arg1SignatureSize + arg2SignatureSize +
                         arg3SignatureSize + arg4SignatureSize +
                         arg5SignatureSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x07;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  memcpy(&signature[offset], arg1SignatureBuffer, arg1SignatureSize);
  offset += arg1SignatureSize;

  memcpy(&signature[offset], arg2SignatureBuffer, arg2SignatureSize);
  offset += arg2SignatureSize;

  memcpy(&signature[offset], arg3SignatureBuffer, arg3SignatureSize);
  offset += arg3SignatureSize;

  memcpy(&signature[offset], arg4SignatureBuffer, arg4SignatureSize);
  offset += arg4SignatureSize;

  memcpy(&signature[offset], arg5SignatureBuffer, arg5SignatureSize);
  offset += arg5SignatureSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      beginP5MemberRef, signature, signatureLength, &beginP5MethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating begin method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(beginP5MethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteBeginMethodWithArguments(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, FunctionMethodArgument* arg1,
    FunctionMethodArgument* arg2, FunctionMethodArgument* arg3,
    FunctionMethodArgument* arg4, FunctionMethodArgument* arg5,
    FunctionMethodArgument* arg6, ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }

  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (beginP6MemberRef == mdMemberRefNil) {
    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength = 18 + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x08;
    signature[offset++] = 0x07;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x02;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x03;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x04;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x05;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x06;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x07;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_beginmethod_name.data(),
        signature, signatureLength, &beginP6MemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper beginP6MemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec beginP6MethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  PCCOR_SIGNATURE arg1SignatureBuffer;
  auto arg1SignatureSize = arg1->GetSignature(arg1SignatureBuffer);

  PCCOR_SIGNATURE arg2SignatureBuffer;
  auto arg2SignatureSize = arg2->GetSignature(arg2SignatureBuffer);

  PCCOR_SIGNATURE arg3SignatureBuffer;
  auto arg3SignatureSize = arg3->GetSignature(arg3SignatureBuffer);

  PCCOR_SIGNATURE arg4SignatureBuffer;
  auto arg4SignatureSize = arg4->GetSignature(arg4SignatureBuffer);

  PCCOR_SIGNATURE arg5SignatureBuffer;
  auto arg5SignatureSize = arg5->GetSignature(arg5SignatureBuffer);

  PCCOR_SIGNATURE arg6SignatureBuffer;
  auto arg6SignatureSize = arg6->GetSignature(arg6SignatureBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize +
                         arg1SignatureSize + arg2SignatureSize +
                         arg3SignatureSize + arg4SignatureSize +
                         arg5SignatureSize + arg6SignatureSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x08;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  memcpy(&signature[offset], arg1SignatureBuffer, arg1SignatureSize);
  offset += arg1SignatureSize;

  memcpy(&signature[offset], arg2SignatureBuffer, arg2SignatureSize);
  offset += arg2SignatureSize;

  memcpy(&signature[offset], arg3SignatureBuffer, arg3SignatureSize);
  offset += arg3SignatureSize;

  memcpy(&signature[offset], arg4SignatureBuffer, arg4SignatureSize);
  offset += arg4SignatureSize;

  memcpy(&signature[offset], arg5SignatureBuffer, arg5SignatureSize);
  offset += arg5SignatureSize;

  memcpy(&signature[offset], arg6SignatureBuffer, arg6SignatureSize);
  offset += arg6SignatureSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      beginP6MemberRef, signature, signatureLength, &beginP6MethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating begin method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(beginP6MethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteBeginMethodWithArgumentsArray(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }
  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (beginArrayMemberRef == mdMemberRefNil) {
    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength = 8 + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x02;
    signature[offset++] = 0x02;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_SZARRAY;
    signature[offset++] = ELEMENT_TYPE_OBJECT;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_beginmethod_name.data(),
        signature, signatureLength, &beginArrayMemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper beginArrayMemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec beginArrayMethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;
  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x02;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      beginArrayMemberRef, signature, signatureLength, &beginArrayMethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating begin method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(beginArrayMethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteEndVoidReturnMemberRef(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }
  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (endVoidMemberRef == mdMemberRefNil) {
    unsigned callTargetReturnVoidBuffer;
    auto callTargetReturnVoidSize = CorSigCompressToken(
        callTargetReturnVoidTypeRef, &callTargetReturnVoidBuffer);

    unsigned exTypeRefBuffer;
    auto exTypeRefSize = CorSigCompressToken(exTypeRef, &exTypeRefBuffer);

    unsigned callTargetStateBuffer;
    auto callTargetStateSize =
        CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

    auto signatureLength =
        8 + callTargetReturnVoidSize + exTypeRefSize + callTargetStateSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x02;
    signature[offset++] = 0x03;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetReturnVoidBuffer,
           callTargetReturnVoidSize);
    offset += callTargetReturnVoidSize;

    signature[offset++] = ELEMENT_TYPE_MVAR;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_CLASS;
    memcpy(&signature[offset], &exTypeRefBuffer, exTypeRefSize);
    offset += exTypeRefSize;

    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
    memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
    offset += callTargetStateSize;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_endmethod_name.data(),
        signature, signatureLength, &endVoidMemberRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper endVoidMemberRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec endVoidMethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;
  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x02;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      endVoidMemberRef, signature, signatureLength, &endVoidMethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating end void method method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(endVoidMethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteEndReturnMemberRef(
    void* rewriterWrapperPtr, mdTypeRef integrationTypeRef,
    const TypeInfo* currentType, FunctionMethodArgument* returnArgument,
    ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }
  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();
  GetTargetReturnValueTypeRef(returnArgument);

  // *** Define base MethodMemberRef for the type

  mdMemberRef endMethodMemberRef = mdMemberRefNil;

  unsigned callTargetReturnTypeRefBuffer;
  auto callTargetReturnTypeRefSize = CorSigCompressToken(
      callTargetReturnTypeRef, &callTargetReturnTypeRefBuffer);

  unsigned exTypeRefBuffer;
  auto exTypeRefSize = CorSigCompressToken(exTypeRef, &exTypeRefBuffer);

  unsigned callTargetStateBuffer;
  auto callTargetStateSize =
      CorSigCompressToken(callTargetStateTypeRef, &callTargetStateBuffer);

  auto signatureLength =
      14 + callTargetReturnTypeRefSize + exTypeRefSize + callTargetStateSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
  signature[offset++] = 0x03;
  signature[offset++] = 0x04;

  signature[offset++] = ELEMENT_TYPE_GENERICINST;
  signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  memcpy(&signature[offset], &callTargetReturnTypeRefBuffer,
         callTargetReturnTypeRefSize);
  offset += callTargetReturnTypeRefSize;
  signature[offset++] = 0x01;
  signature[offset++] = ELEMENT_TYPE_MVAR;
  signature[offset++] = 0x02;

  signature[offset++] = ELEMENT_TYPE_MVAR;
  signature[offset++] = 0x01;

  signature[offset++] = ELEMENT_TYPE_MVAR;
  signature[offset++] = 0x02;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &exTypeRefBuffer, exTypeRefSize);
  offset += exTypeRefSize;

  signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  memcpy(&signature[offset], &callTargetStateBuffer, callTargetStateSize);
  offset += callTargetStateSize;

  hr = module_metadata->metadata_emit->DefineMemberRef(
      callTargetTypeRef, managed_profiler_calltarget_endmethod_name.data(),
      signature, signatureLength, &endMethodMemberRef);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Wrapper endMethodMemberRef could not be defined.");
    return hr;
  }

  // *** Define Method Spec

  mdMethodSpec endMethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  PCCOR_SIGNATURE returnSignatureBuffer;
  auto returnSignatureLength =
      returnArgument->GetSignature(returnSignatureBuffer);

  signatureLength =
      4 + integrationTypeSize + currentTypeSize + returnSignatureLength;
  signature = new COR_SIGNATURE[signatureLength];
  offset = 0;

  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x03;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  memcpy(&signature[offset], returnSignatureBuffer, returnSignatureLength);
  offset += returnSignatureLength;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      endMethodMemberRef, signature, signatureLength, &endMethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating end method member spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(endMethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteLogException(void* rewriterWrapperPtr,
                                            mdTypeRef integrationTypeRef,
                                            const TypeInfo* currentType,
                                            ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return hr;
  }
  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  if (logExceptionRef == mdMemberRefNil) {
    unsigned exTypeRefBuffer;
    auto exTypeRefSize = CorSigCompressToken(exTypeRef, &exTypeRefBuffer);

    auto signatureLength = 5 + exTypeRefSize;
    auto* signature = new COR_SIGNATURE[signatureLength];
    unsigned offset = 0;

    signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERIC;
    signature[offset++] = 0x02;
    signature[offset++] = 0x01;

    signature[offset++] = ELEMENT_TYPE_VOID;
    signature[offset++] = ELEMENT_TYPE_CLASS;
    memcpy(&signature[offset], &exTypeRefBuffer, exTypeRefSize);
    offset += exTypeRefSize;

    auto hr = module_metadata->metadata_emit->DefineMemberRef(
        callTargetTypeRef, managed_profiler_calltarget_logexception_name.data(),
        signature, signatureLength, &logExceptionRef);
    delete[] signature;
    if (FAILED(hr)) {
      Warn("Wrapper logExceptionRef could not be defined.");
      return hr;
    }
  }

  mdMethodSpec logExceptionMethodSpec = mdMethodSpecNil;

  unsigned integrationTypeBuffer;
  ULONG integrationTypeSize =
      CorSigCompressToken(integrationTypeRef, &integrationTypeBuffer);

  bool isValueType = currentType->valueType;
  mdToken currentTypeRef = GetCurrentTypeRef(currentType, isValueType);

  unsigned currentTypeBuffer;
  ULONG currentTypeSize =
      CorSigCompressToken(currentTypeRef, &currentTypeBuffer);

  auto signatureLength = 4 + integrationTypeSize + currentTypeSize;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;
  signature[offset++] = IMAGE_CEE_CS_CALLCONV_GENERICINST;
  signature[offset++] = 0x02;

  signature[offset++] = ELEMENT_TYPE_CLASS;
  memcpy(&signature[offset], &integrationTypeBuffer, integrationTypeSize);
  offset += integrationTypeSize;

  if (isValueType) {
    signature[offset++] = ELEMENT_TYPE_VALUETYPE;
  } else {
    signature[offset++] = ELEMENT_TYPE_CLASS;
  }
  memcpy(&signature[offset], &currentTypeBuffer, currentTypeSize);
  offset += currentTypeSize;

  hr = module_metadata->metadata_emit->DefineMethodSpec(
      logExceptionRef, signature, signatureLength, &logExceptionMethodSpec);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Error creating log exception method spec.");
    return hr;
  }

  *instruction = rewriterWrapper->CallMember(logExceptionMethodSpec, false);
  return S_OK;
}

HRESULT CallTargetTokens::WriteCallTargetReturnGetReturnValue(
    void* rewriterWrapperPtr, mdTypeSpec callTargetReturnTypeSpec,
    ILInstr** instruction) {
  auto hr = EnsureBaseCalltargetTokens();
  if (FAILED(hr)) {
    return mdMemberRefNil;
  }
  ILRewriterWrapper* rewriterWrapper = (ILRewriterWrapper*)rewriterWrapperPtr;
  ModuleMetadata* module_metadata = GetMetadata();

  // Ensure T CallTargetReturn<T>.GetReturnValue() member ref
  mdMemberRef callTargetReturnGetValueMemberRef = mdMemberRefNil;

  auto signatureLength = 4;
  auto* signature = new COR_SIGNATURE[signatureLength];
  unsigned offset = 0;

  signature[offset++] =
      IMAGE_CEE_CS_CALLCONV_DEFAULT | IMAGE_CEE_CS_CALLCONV_HASTHIS;
  signature[offset++] = 0x00;
  signature[offset++] = ELEMENT_TYPE_VAR;
  signature[offset++] = 0x00;
  hr = module_metadata->metadata_emit->DefineMemberRef(
      callTargetReturnTypeSpec,
      managed_profiler_calltarget_returntype_getreturnvalue_name.data(),
      signature, signatureLength, &callTargetReturnGetValueMemberRef);
  delete[] signature;
  if (FAILED(hr)) {
    Warn("Wrapper callTargetReturnGetValueMemberRef could not be defined.");
    return mdMemberRefNil;
  }

  *instruction = rewriterWrapper->CallMember(callTargetReturnGetValueMemberRef, false);
  return S_OK;
}

}  // namespace trace