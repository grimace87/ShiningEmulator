#pragma once

#include <Windows.h>
#include <ShObjIdl_core.h>

class WindowsFileHelper : public IFileDialogEvents {
    // From IFileDialogEvents
    HRESULT STDMETHODCALLTYPE OnFileOk(
            /* [in] */ __RPC__in_opt IFileDialog *pfd) override;
    HRESULT STDMETHODCALLTYPE OnFolderChanging(
            /* [in] */ __RPC__in_opt IFileDialog *pfd,
    /* [in] */ __RPC__in_opt IShellItem *psiFolder) override;
    HRESULT STDMETHODCALLTYPE OnFolderChange(
            /* [in] */ __RPC__in_opt IFileDialog *pfd) override;
    HRESULT STDMETHODCALLTYPE OnSelectionChange(
            /* [in] */ __RPC__in_opt IFileDialog *pfd) override;
    HRESULT STDMETHODCALLTYPE OnShareViolation(
            /* [in] */ __RPC__in_opt IFileDialog *pfd,
    /* [in] */ __RPC__in_opt IShellItem *psi,
    /* [out] */ __RPC__out FDE_SHAREVIOLATION_RESPONSE *pResponse) override;
    HRESULT STDMETHODCALLTYPE OnTypeChange(
            /* [in] */ __RPC__in_opt IFileDialog *pfd) override;
    HRESULT STDMETHODCALLTYPE OnOverwrite(
            /* [in] */ __RPC__in_opt IFileDialog *pfd,
    /* [in] */ __RPC__in_opt IShellItem *psi,
    /* [out] */ __RPC__out FDE_OVERWRITE_RESPONSE *pResponse) override;

    // From IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(
            /* [in] */ REFIID riid,
    /* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
};
