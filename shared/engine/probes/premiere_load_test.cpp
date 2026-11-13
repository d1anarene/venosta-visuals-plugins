// Load/symbol check only. Does not invoke Adobe callbacks or prove host integration.
#include <cstdio>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
void* open(const char* p){return reinterpret_cast<void*>(LoadLibraryA(p));}
void* symbol(void* h,const char* p){return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h),p));}
#else
#include <dlfcn.h>
void* open(const char* p){return dlopen(p,RTLD_NOW|RTLD_LOCAL);}
void* symbol(void* h,const char* p){return dlsym(h,p);}
#endif
int main(int argc,char** argv){
  if(argc<2)return 2;
  void* h=open(argv[1]);if(!h){std::fprintf(stderr,"load failed: %s\n",argv[1]);return 1;}
  for(const char* name:{"EffectMain","PluginDataEntryFunction2"})if(!symbol(h,name)){std::fprintf(stderr,"missing %s\n",name);return 1;}
  if(argc>2&&!symbol(h,"xGPUFilterEntry")){std::fprintf(stderr,"GPU entry missing\n");return 1;}
  std::puts("Premiere probe binary loaded; required entry points present (host behaviour untested)");return 0;
}
