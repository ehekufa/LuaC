#ifndef LUAC_FFI_H
#define LUAC_FFI_H
#include <stddef.h>
/* Minimal dynamic FFI. libffi is discovered at runtime: generated programs do
   not need libffi headers or a link-time dependency. Target libraries still
   use the platform's normal DLL/SO search and dependency rules. */
struct LcFFILibrary { LcObject object; void *handle; char *path; int closed; };
typedef struct { size_t size; unsigned short alignment, type; void *elements; } LcFFIType;
typedef struct { unsigned abi, nargs; LcFFIType **arg_types, *return_type; unsigned bytes, flags; } LcFFICifPrefix;
typedef int (*LcFFIPrep)(void *, unsigned, unsigned, LcFFIType *, LcFFIType **);
typedef void (*LcFFICall)(void *, void *, void *, void **);
static int lc_ffi_ready;
static void *lc_ffi_handle;
static LcFFIPrep lc_ffi_prep;
static LcFFICall lc_ffi_call_native;
static void *lc_ffi_symbol(void *handle, const char *name) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)handle, name);
#else
    return dlsym(handle, name);
#endif
}
static void lc_ffi_open_backend(void) {
    const char *error;
    if (lc_ffi_ready) return; lc_ffi_ready = -1;
#ifdef _WIN32
    { const char *names[]={"libffi-8.dll","libffi-7.dll","libffi.dll",NULL}; int i;
      lc_ffi_prep=(LcFFIPrep)lc_ffi_symbol(GetModuleHandleA(NULL),"ffi_prep_cif");
      lc_ffi_call_native=(LcFFICall)lc_ffi_symbol(GetModuleHandleA(NULL),"ffi_call");
      for(i=0;(!lc_ffi_prep||!lc_ffi_call_native)&&names[i];++i){lc_ffi_handle=LoadLibraryA(names[i]);if(lc_ffi_handle){lc_ffi_prep=(LcFFIPrep)lc_ffi_symbol(lc_ffi_handle,"ffi_prep_cif");lc_ffi_call_native=(LcFFICall)lc_ffi_symbol(lc_ffi_handle,"ffi_call");}}
    }
#else
    { const char *names[]={"libffi.so.8","libffi.so.7","libffi.so","libffi.dylib","libffi.8.dylib",NULL}; int i;
      lc_ffi_prep=(LcFFIPrep)lc_ffi_symbol(RTLD_DEFAULT,"ffi_prep_cif");lc_ffi_call_native=(LcFFICall)lc_ffi_symbol(RTLD_DEFAULT,"ffi_call");
      for(i=0;(!lc_ffi_prep||!lc_ffi_call_native)&&names[i];++i){lc_ffi_handle=dlopen(names[i],RTLD_NOW|RTLD_LOCAL);if(lc_ffi_handle){lc_ffi_prep=(LcFFIPrep)lc_ffi_symbol(lc_ffi_handle,"ffi_prep_cif");lc_ffi_call_native=(LcFFICall)lc_ffi_symbol(lc_ffi_handle,"ffi_call");}}
    }
#endif
    if(lc_ffi_prep&&lc_ffi_call_native)lc_ffi_ready=1;
    error=lc_ffi_ready==1?NULL:"libffi runtime not found (install libffi8/libffi)";(void)error;
}
static void lc_ffi_dispose(LcFFILibrary *library) {
    if(!library||library->closed)return;library->closed=1;
#ifdef _WIN32
    if(library->handle)FreeLibrary((HMODULE)library->handle);
#else
    if(library->handle)dlclose(library->handle);
#endif
    library->handle=NULL;free(library->path);library->path=NULL;
}
static LcValue lc_ffi_library_value(LcFFILibrary *library){LcValue v=lc_nil();v.type=LC_FFI;v.as.ffi=library;return v;}
static LcValue lc_ffi_open(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcFFILibrary *library;const char *path;void *handle;const char *error;(void)self;
    if(argc<1||lc_first_value(argv[0]).type!=LC_STRING){lc_error(ctx,"ffi.open expects a library path");return lc_nil();}
    path=lc_first_value(argv[0]).as.string->data;
#ifdef _WIN32
    handle=(void *)LoadLibraryA(path);error=handle?NULL:"cannot load DLL";
#else
    handle=dlopen(path,RTLD_NOW|RTLD_LOCAL);error=handle?NULL:dlerror();
#endif
    if(!handle){lc_error(ctx,"ffi.open('%s'): %s",path,error?error:"library not found");return lc_nil();}
    library=(LcFFILibrary *)lc_alloc(ctx,sizeof(*library),LC_FFI);if(!library){
#ifdef _WIN32
        FreeLibrary((HMODULE)handle);
#else
        dlclose(handle);
#endif
        return lc_nil();}
    library->handle=handle;library->path=(char *)malloc(strlen(path)+1);if(!library->path){lc_release(ctx,lc_ffi_library_value(library));lc_error(ctx,"out of memory");return lc_nil();}strcpy(library->path,path);return lc_ffi_library_value(library);
}
static LcValue lc_ffi_close(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){
    LcValue value=argc?lc_first_value(argv[0]):lc_nil();(void)self;if(value.type!=LC_FFI){lc_error(ctx,"ffi library expected");return lc_nil();}lc_ffi_dispose(value.as.ffi);return lc_bool(1);
}
static int lc_ffi_type(LcContext *ctx,const char **text,LcFFIType *type,int *kind){
    struct Def{const char *name;int code,kind;size_t size,align;};
    static const struct Def defs[]={
      {"void",0,0,0,0},{"bool",5,1,1,1},{"int8",6,2,1,1},{"uint8",5,3,1,1},{"int16",8,4,2,2},{"uint16",7,5,2,2},
      {"int32",10,6,4,4},{"int",10,6,4,4},{"uint32",9,7,4,4},{"uint",9,7,4,4},{"int64",12,8,8,8},{"uint64",11,9,8,8},
      {"float",2,10,4,4},{"double",3,11,8,8},{"pointer",14,12,sizeof(void *),sizeof(void *)},{"string",14,13,sizeof(char *),sizeof(char *)},
      {"size_t",11,9,sizeof(size_t),sizeof(size_t)},{"ptrdiff_t",12,8,sizeof(ptrdiff_t),sizeof(ptrdiff_t)},{NULL,0,0,0,0}};
    int i;while(isspace((unsigned char)**text))++*text;
    for(i=0;defs[i].name;++i){size_t n=strlen(defs[i].name);if(!strncmp(*text,defs[i].name,n)&&!isalnum((unsigned char)(*text)[n])&&(*text)[n]!='_'){
      *text+=n;type->size=defs[i].size;type->alignment=(unsigned short)defs[i].align;type->type=(unsigned short)defs[i].code;type->elements=NULL;*kind=defs[i].kind;return 1;}}
    if(!strncmp(*text,"ptr",3)&&!isalnum((unsigned char)(*text)[3])){*text+=3;type->size=sizeof(void *);type->alignment=sizeof(void *);type->type=14;type->elements=NULL;*kind=12;return 1;}
    lc_error(ctx,"unknown FFI type near '%s'",*text);return 0;
}
static int lc_ffi_signature(LcContext *ctx,const char *signature,LcFFIType *ret,int *retkind,LcFFIType *args,int *kinds,int *count){
    const char *p=signature;if(!lc_ffi_type(ctx,&p,ret,retkind))return 0;while(isspace((unsigned char)*p))++p;if(*p!='('){lc_error(ctx,"FFI signature must be return_type(arg_type,...)");return 0;}++p;*count=0;
    for(;;){while(isspace((unsigned char)*p))++p;if(*p==')'){++p;break;}if(*count>=16){lc_error(ctx,"FFI supports at most 16 arguments");return 0;}if(!lc_ffi_type(ctx,&p,&args[*count],&kinds[*count]))return 0;++*count;while(isspace((unsigned char)*p))++p;if(*p==','){++p;continue;}if(*p==')'){++p;break;}lc_error(ctx,"malformed FFI signature");return 0;}
    while(isspace((unsigned char)*p))++p;if(*p){lc_error(ctx,"unexpected text after FFI signature");return 0;}return 1;
}
static LcValue lc_ffi_invoke(LcContext *ctx,LcValue library,LcValue symbol,LcValue signature,int argc,LcValue *argv){
    LcFFIType ret,types[16];LcFFIType *typeptrs[16];void *values[16];void *function;unsigned char storage[16][16];unsigned char result[32];unsigned char cif[256];int kinds[16],retkind,nargs,i,ok;int64_t iv;double dv;
    if(library.type!=LC_FFI||library.as.ffi->closed||symbol.type!=LC_STRING||signature.type!=LC_STRING){lc_error(ctx,"ffi.call expects library, symbol and signature strings");return lc_nil();}
    if(!lc_ffi_signature(ctx,signature.as.string->data,&ret,&retkind,types,kinds,&nargs))return lc_nil();
    if(argc!=nargs){lc_error(ctx,"FFI signature expects %d arguments, got %d",nargs,argc);return lc_nil();}
    function=lc_ffi_symbol(library.as.ffi->handle,symbol.as.string->data);if(!function){lc_error(ctx,"FFI symbol '%s' not found",symbol.as.string->data);return lc_nil();}
    lc_ffi_open_backend();if(lc_ffi_ready!=1){lc_error(ctx,"FFI calls require a system libffi runtime");return lc_nil();}
    memset(cif,0,sizeof(cif));memset(result,0,sizeof(result));memset(storage,0,sizeof(storage));
    for(i=0;i<nargs;++i){LcValue v=lc_first_value(argv[i]);typeptrs[i]=&types[i];values[i]=storage[i];
      if(kinds[i]==10||kinds[i]==11){dv=lc_to_number(ctx,v,&ok);if(!ok)return lc_nil();if(kinds[i]==10){float f=(float)dv;memcpy(storage[i],&f,4);}else memcpy(storage[i],&dv,8);}
      else if(kinds[i]==12){if(v.type==LC_STRING){void *p=v.as.string->data;memcpy(storage[i],&p,sizeof(p));}else if(v.type==LC_NIL){void *p=NULL;memcpy(storage[i],&p,sizeof(p));}else{iv=lc_to_integer(ctx,v);if(ctx->error)return lc_nil();memcpy(storage[i],&iv,sizeof(iv));}}
      else if(kinds[i]==13){const char *p=NULL;if(v.type==LC_STRING)p=v.as.string->data;else if(v.type!=LC_NIL){lc_error(ctx,"string or nil expected for FFI string argument");return lc_nil();}memcpy(storage[i],&p,sizeof(p));}
      else if(kinds[i]==1){int b=lc_truthy(v);memcpy(storage[i],&b,1);}
      else{iv=lc_to_integer(ctx,v);if(ctx->error)return lc_nil();if(kinds[i]==2||kinds[i]==3)memcpy(storage[i],&iv,1);else if(kinds[i]==4||kinds[i]==5)memcpy(storage[i],&iv,2);else if(kinds[i]==6||kinds[i]==7)memcpy(storage[i],&iv,4);else memcpy(storage[i],&iv,8);}
    }
#if defined(__x86_64__) || defined(_M_X64)
#define LC_FFI_DEFAULT_ABI 2
#else
#define LC_FFI_DEFAULT_ABI 1
#endif
    {LcFFICifPrefix prefix;memset(&prefix,0,sizeof(prefix));memcpy(cif,&prefix,sizeof(prefix));(void)prefix;}
    if(lc_ffi_prep(cif,LC_FFI_DEFAULT_ABI,(unsigned)nargs,&ret,typeptrs)!=0){lc_error(ctx,"libffi rejected the signature");return lc_nil();}
    lc_ffi_call_native(cif,function,result,values);
    if(retkind==0)return lc_nil();if(retkind==1)return lc_bool(result[0]!=0);if(retkind==10){float f;memcpy(&f,result,4);return lc_number(f);}if(retkind==11){memcpy(&dv,result,8);return lc_number(dv);}if(retkind==13){const char *p;memcpy(&p,result,sizeof(p));return p?lc_string(ctx,p):lc_nil();}if(retkind==12){void *p;memcpy(&p,result,sizeof(p));return lc_integer((int64_t)(intptr_t)p);}
    iv=0;if(retkind==2)iv=*(signed char *)result;else if(retkind==3)iv=*(unsigned char *)result;else if(retkind==4)iv=*(int16_t *)result;else if(retkind==5)iv=*(uint16_t *)result;else if(retkind==6)iv=*(int32_t *)result;else if(retkind==7)iv=*(uint32_t *)result;else memcpy(&iv,result,8);return lc_integer(iv);
}
static LcValue lc_ffi_call(LcContext *ctx,LcFunction *self,int argc,LcValue *argv){(void)self;if(argc<3){lc_error(ctx,"ffi.call expects library, symbol, signature, ...");return lc_nil();}return lc_ffi_invoke(ctx,lc_first_value(argv[0]),lc_first_value(argv[1]),lc_first_value(argv[2]),argc-3,argv+3);}
#endif
