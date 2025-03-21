// Main mod related code goes here
#include "pch.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include "detours.h"
#include "Helpers.h"
#include <cstdio>
#include "toml.hpp"
#include <vector>
#include <string>
#include <sys/stat.h>

char dllBasePath[256];

FILE* logfile;
toml::table config;

VTABLE_HOOK(HRESULT, STDMETHODCALLTYPE, ID3D11Device, CreatePixelShader, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader) {
    int* pShaderHashi = (int*)((char*)pShaderBytecode + 4);
    unsigned char* pShaderHash = ((unsigned char*)pShaderBytecode + 4);
#ifdef _DEBUG
    fprintf(logfile, "Attempting to get filename of replacement shader %d %d %d %d\n", pShaderHashi[0], pShaderHashi[1], pShaderHashi[2], pShaderHashi[3]);
    fflush(logfile);
#endif
    char hashStrBuf[32] = {'\0'};

    sprintf(hashStrBuf, "shader_%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", pShaderHash[0], pShaderHash[1], pShaderHash[2], pShaderHash[3],
        pShaderHash[4], pShaderHash[5], pShaderHash[6], pShaderHash[7],
        pShaderHash[8], pShaderHash[9], pShaderHash[10], pShaderHash[11],
        pShaderHash[12], pShaderHash[13], pShaderHash[14], pShaderHash[15]);
#ifdef _DEBUG
    fprintf(logfile, "Loading from var %s\n", hashStrBuf);
    fflush(logfile);
#endif
    std::string replacementFileName = config[hashStrBuf].value_or("");

    if (replacementFileName.empty()) {
#ifdef _DEBUG
        fprintf(logfile, "No shader found, loading original.\n");
        fflush(logfile);
#endif
        return originalID3D11DeviceCreatePixelShader(This, pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);

    }
    else {


        char pathBuf[512] = { '\0' };
        sprintf(pathBuf, "%s/Shader/cso/%s", dllBasePath, replacementFileName.c_str());
#ifdef _DEBUG
        fprintf(logfile, "Attempting to load shader from %s.\n", pathBuf);
        fflush(logfile);
#endif
        struct stat csoStat;
        int sres = stat(pathBuf, &csoStat);
#ifdef _DEBUG
        fprintf(logfile, "File reports size %d\n", csoStat.st_size);
        fflush(logfile);
#endif
        if (sres != ENOENT) {
#ifdef _DEBUG
            fprintf(logfile, "Loading shader @ %s\n", pathBuf);
            fflush(logfile);
#endif
            FILE* csoFile = fopen(pathBuf, "rb");
            char* csoBuf = (char*)malloc(csoStat.st_size);
            fread(csoBuf, 1, csoStat.st_size, csoFile);
            fclose(csoFile);

            int* pNewShaderHash = (int*)(csoBuf + 4);
#ifdef _DEBUG
            fprintf(logfile, "Loaded shader has hash of %d %d %d %d\n", pNewShaderHash[0], pNewShaderHash[1], pNewShaderHash[2], pNewShaderHash[3]);
            fflush(logfile);
#endif

            HRESULT res = originalID3D11DeviceCreatePixelShader(This, csoBuf, csoStat.st_size, pClassLinkage, ppPixelShader);

#ifdef _DEBUG
            if (FAILED(res)) {
                fprintf(logfile, "Failed to create Pixel Shader for CSO with hash %d %d %d %d, HRESULT : %x\n", pNewShaderHash[0], pNewShaderHash[1], pNewShaderHash[2], pNewShaderHash[3], res);
                fflush(logfile);
            }
#endif

            free(csoBuf);

            return res;
        }
    }
}

extern "C" void __declspec(dllexport) Init() {
    config = toml::parse_file("config.toml");
#ifdef _DEBUG
    logfile = fopen("./logfile.txt", "w");
#endif

    GetCurrentDirectoryA(256, dllBasePath);
}

extern "C" void __declspec(dllexport) D3DInit(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
    INSTALL_VTABLE_HOOK(ID3D11Device, device, CreatePixelShader, 15);
}