#ifndef ADDREF_H
#define ADDREF_H

#ifdef DXC_MICROCOM_REF_FIELD
#undef DXC_MICROCOM_REF_FIELD
#endif
#define DXC_MICROCOM_REF_FIELD(m_dwRef) volatile ULONG m_dwRef = 0;


#ifdef DXC_MICROCOM_ADDREF_IMPL
#undef DXC_MICROCOM_ADDREF_IMPL
#endif
#define DXC_MICROCOM_ADDREF_IMPL(m_dwRef) \
     ULONG STDMETHODCALLTYPE AddRef() {\
        return InterlockedIncrement(&m_dwRef); \
     }

#ifdef DXC_MICROCOM_ADDREF_RELEASE_IMPL
#undef DXC_MICROCOM_ADDREF_RELEASE_IMPL
#endif
#define DXC_MICROCOM_ADDREF_RELEASE_IMPL(m_dwRef) \
     DXC_MICROCOM_ADDREF_IMPL(m_dwRef) \
     ULONG STDMETHODCALLTYPE Release() { \
        ULONG result = InterlockedDecrement(&m_dwRef); \
         if (result == 0) delete this; \
         return result; \
     }

 // The "TM" version keep an IMalloc field that, if not null, indicate
 // ownership of 'this' and of any allocations used during release.
#ifdef DXC_MICROCOM_TM_REF_FIELDS
#undef DXC_MICROCOM_TM_REF_FIELDS
#endif
#define DXC_MICROCOM_TM_REF_FIELDS() \
  volatile ULONG m_dwRef = 0;\
   CComPtr<IMalloc> m_pMalloc;

#ifdef DXC_MICROCOM_TM_ADDREF_RELEASE_IMPL
#undef DXC_MICROCOM_TM_ADDREF_RELEASE_IMPL
#endif
#define DXC_MICROCOM_TM_ADDREF_RELEASE_IMPL() \
    DXC_MICROCOM_ADDREF_IMPL(m_dwRef) \
    ULONG STDMETHODCALLTYPE Release() { \
      ULONG result = InterlockedDecrement(&m_dwRef); \
      if (result == 0) { \
        CComPtr<IMalloc> pTmp(m_pMalloc); \
        DxcThreadMalloc M(pTmp); \
        DxcCallDestructor(this); \
        pTmp->Free(this); \
      } \
      return result; \
    }

#endif
