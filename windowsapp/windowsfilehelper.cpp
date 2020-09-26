#include "windowsfilehelper.h"

HRESULT STDMETHODCALLTYPE WindowsFileHelper::OnFileOk(__RPC__in_opt IFileDialog *pfd) {
    // Called just before the dialog returns with a result - return S_OK (S_FALSE would prevent the dialog from closing)
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WindowsFileHelper::OnFolderChanging(__RPC__in_opt IFileDialog *pfd, __RPC__in_opt IShellItem *psiFolder) {
    // Called before OnFolderChange to provide a chance to prevent navigation
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WindowsFileHelper::OnFolderChange(__RPC__in_opt IFileDialog *pfd) {
    // User navigates to a new folder
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WindowsFileHelper::OnSelectionChange(__RPC__in_opt IFileDialog *pfd) {
    // User changes selection in the dialog view
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WindowsFileHelper::OnShareViolation(__RPC__in_opt IFileDialog *pfd, __RPC__in_opt IShellItem *psi, __RPC__out FDE_SHAREVIOLATION_RESPONSE *pResponse) {
    // Allow responding to share violations in open or save dialog
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WindowsFileHelper::OnTypeChange(__RPC__in_opt IFileDialog *pfd) {
    // Called when dialog is opened to notify app of initially-chosen file type
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WindowsFileHelper::OnOverwrite(__RPC__in_opt IFileDialog *pfd, __RPC__in_opt IShellItem *psi, __RPC__out FDE_OVERWRITE_RESPONSE *pResponse) {
    // Not used in open dialog
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WindowsFileHelper::QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject) {
    return E_NOTIMPL;
}

ULONG STDMETHODCALLTYPE WindowsFileHelper::AddRef() {
    return E_NOTIMPL;
}

ULONG STDMETHODCALLTYPE WindowsFileHelper::Release() {
    return E_NOTIMPL;
}
