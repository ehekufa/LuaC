#ifndef LUAC_TCC_WINCRYPT_H
#define LUAC_TCC_WINCRYPT_H
/* Windows CryptoAPI declarations used to read trusted certificates. */
typedef void *HCERTSTORE;
typedef struct _CERT_CONTEXT {
    DWORD dwCertEncodingType;
    BYTE *pbCertEncoded;
    DWORD cbCertEncoded;
    void *pCertInfo;
    HCERTSTORE hCertStore;
} CERT_CONTEXT, *PCERT_CONTEXT;
typedef const CERT_CONTEXT *PCCERT_CONTEXT;
__declspec(dllimport) HCERTSTORE WINAPI CertOpenSystemStoreA(HANDLE, LPCSTR);
__declspec(dllimport) PCCERT_CONTEXT WINAPI CertEnumCertificatesInStore(HCERTSTORE, PCCERT_CONTEXT);
__declspec(dllimport) BOOL WINAPI CertCloseStore(HCERTSTORE, DWORD);
#endif
