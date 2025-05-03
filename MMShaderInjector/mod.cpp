// Main mod related code goes here
#include "pch.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include "detours.h"
#include "Helpers.h"
#include <cstdio>
#include "toml.hpp"
#include <map>
#include <vector>
#include <string>
#include <sys/stat.h>

char dllBasePath[256];

FILE* logfile;
toml::table config;

std::map<ID3D11PixelShader*, std::string> shaderMap;
std::map<ID3D11PixelShader*, ID3D11Buffer*> shaderUniformBufferMap;
std::map<ID3D11PixelShader*, D3D11_SUBRESOURCE_DATA> shaderUniformSubresDataMap;
std::map<ID3D11PixelShader*, uint8_t*> shaderUniformBufferDataMap;

VTABLE_HOOK(HRESULT, STDMETHODCALLTYPE, ID3D11Device, CreatePixelShader, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader) {
    int* pShaderHashi = (int*)((char*)pShaderBytecode + 4);
    unsigned char* pShaderHash = ((unsigned char*)pShaderBytecode + 4);
#ifdef _DEBUG
    fprintf(logfile, "Attempting to get filename of replacement shader %d %d %d %d\n", pShaderHashi[0], pShaderHashi[1], pShaderHashi[2], pShaderHashi[3]);
    fflush(logfile);
#endif
    char hashStrBuf[64] = {'\0'};

    sprintf(hashStrBuf, "fragment_%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", pShaderHash[0], pShaderHash[1], pShaderHash[2], pShaderHash[3],
        pShaderHash[4], pShaderHash[5], pShaderHash[6], pShaderHash[7],
        pShaderHash[8], pShaderHash[9], pShaderHash[10], pShaderHash[11],
        pShaderHash[12], pShaderHash[13], pShaderHash[14], pShaderHash[15]);
#ifdef _DEBUG
    fprintf(logfile, "Loading from var %s\n", hashStrBuf);
    fflush(logfile);
#endif
    std::string replacementFileName;

    if (auto shader = config[hashStrBuf].as_table()) {
#ifdef _DEBUG
        fprintf(logfile, "shader table opened\n");
        fflush(logfile);
#endif
        replacementFileName = shader->at("filename").value_or("");
        if (replacementFileName != "") {
#ifdef _DEBUG
            fprintf(logfile, "Creating uniform buffer data\n");
            fflush(logfile);

#endif
            int uniformBufferSize = shader->at("uniform_buffer_size").value_or(0);

            if (auto uniforms = shader->at("uniforms").as_array()) {

                // now create the DESC
                D3D11_BUFFER_DESC desc;
                desc.ByteWidth = uniformBufferSize;
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

                D3D11_SUBRESOURCE_DATA subresData;

                uint8_t* uniformBuffer = (uint8_t*)malloc(uniformBufferSize);
                for (const auto& uniform_node : *uniforms) {
                    if (const auto* uniform = uniform_node.as_table()) {
                        int size = uniform->at("size").value_or(0);
                        std::string dataType = uniform->at("data_type").value_or("float");
                        int offset = uniform->at("offset").value_or(0);

                        if (auto values = uniform->at("values").as_array()) {
                            for (int i = 0; i < 4; i++) {
                                if (i < size) {
                                    if (dataType == "int") {
                                        int v;
                                        if ((*values)[i].is_floating_point()) {
                                            v = static_cast<int>((*values)[i].value<double>().value_or(0.0));
                                        }
                                        else if ((*values)[i].is_integer()) {
                                            v = static_cast<int>((*values)[i].value<int64_t>().value_or(0));
                                        }
                                        memcpy(uniformBuffer + (offset * 16) + (i * 4), &v, 4);
                                    }
                                    else if (dataType == "uint") {
                                        unsigned int v;
                                        if ((*values)[i].is_floating_point()) {
                                            v = static_cast<unsigned int>(static_cast<int>((*values)[i].value<double>().value_or(0.0)));
                                        }
                                        else if ((*values)[i].is_integer()) {
                                            v = static_cast<unsigned int>((*values)[i].value<uint64_t>().value_or(0));
                                        }
                                        memcpy(uniformBuffer + (offset * 16) + (i * 4), &v, 4);
                                    }
                                    else if (dataType == "float") {
                                        float v;
                                        if ((*values)[i].is_floating_point()) {
                                            v = static_cast<float>((*values)[i].value<double>().value_or(0.0));
                                        }
                                        else if ((*values)[i].is_integer()) {
                                            v = static_cast<float>((*values)[i].value<uint64_t>().value_or(0));
                                        }
#ifdef _DEBUG
                                        fprintf(logfile, "Writing float %3.6f to constant buffer\n", v);
                                        fflush(logfile);
#endif
                                        memcpy(uniformBuffer + (offset * 16) + (i * 4), &v, 4);
                                    }
                                }
                                else {
                                    int v = 0;
                                    memcpy(uniformBuffer + (offset * 16) + (i * 4), &v, 4);
                                }
                            }
                        }
                        else {
                            for (int i = 0; i < 4; i++) {
                                int v = 0;
                                memcpy(uniformBuffer + (offset * 16) + (i * 4), &v, 4);
                            }
                        }
#ifdef _DEBUG
                        fprintf(logfile, "Uniform data written to buffer\n");
                        fflush(logfile);

#endif
                    }
                }

                subresData.pSysMem = uniformBuffer;

                shaderUniformBufferDataMap.emplace(*ppPixelShader, uniformBuffer);  // should ensure that this doesn't get destroyed. should also maintain the data.
                shaderUniformSubresDataMap.emplace(*ppPixelShader, subresData);

                ID3D11Buffer* pBuffer;

                This->CreateBuffer(&desc, &subresData, &pBuffer);

                shaderUniformBufferMap.emplace(*ppPixelShader, pBuffer);
            }
        }
    }

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

            shaderMap.emplace(*ppPixelShader, hashStrBuf);

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

VTABLE_HOOK(HRESULT, STDMETHODCALLTYPE, ID3D11Device, CreateVertexShader, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader) {
    int* pShaderHashi = (int*)((char*)pShaderBytecode + 4);
    unsigned char* pShaderHash = ((unsigned char*)pShaderBytecode + 4);
#ifdef _DEBUG
    fprintf(logfile, "Attempting to get filename of replacement shader %d %d %d %d\n", pShaderHashi[0], pShaderHashi[1], pShaderHashi[2], pShaderHashi[3]);
    fflush(logfile);
#endif
    char hashStrBuf[64] = { '\0' };

    sprintf(hashStrBuf, "vertex_%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", pShaderHash[0], pShaderHash[1], pShaderHash[2], pShaderHash[3],
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
        return originalID3D11DeviceCreateVertexShader(This, pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);

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

            HRESULT res = originalID3D11DeviceCreateVertexShader(This, csoBuf, csoStat.st_size, pClassLinkage, ppVertexShader);

#ifdef _DEBUG
            if (FAILED(res)) {
                fprintf(logfile, "Failed to create Vertex Shader for CSO with hash %d %d %d %d, HRESULT : %x\n", pNewShaderHash[0], pNewShaderHash[1], pNewShaderHash[2], pNewShaderHash[3], res);
                fflush(logfile);
            }
#endif

            free(csoBuf);

            return res;
        }
    }
}

VTABLE_HOOK(HRESULT, STDMETHODCALLTYPE, ID3D11DeviceContext, PSSetShader, ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) {
    // check if pPixelShader in shader map
    HRESULT res = originalID3D11DeviceContextPSSetShader(This, pPixelShader, ppClassInstances, NumClassInstances);
    if (shaderMap.contains(pPixelShader)) {
#ifdef _DEBUG
        fprintf(logfile, "Binding shader that has been loaded externally, attempting to load uniforms\n");
        fflush(logfile);
#endif

        ID3D11Buffer* buf = shaderUniformBufferMap[pPixelShader];

        This->PSSetConstantBuffers(8, 1, &(shaderUniformBufferMap[pPixelShader]));
    }
    return res;
}

extern "C" void __declspec(dllexport) Init() {
    config = toml::parse_file("config.toml");
#ifdef _DEBUG
    logfile = fopen("./logfile.txt", "w");
#endif

    GetCurrentDirectoryA(256, dllBasePath);
}

extern "C" void __declspec(dllexport) D3DInit(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
    INSTALL_VTABLE_HOOK(ID3D11Device, device, CreateVertexShader, 12);
    INSTALL_VTABLE_HOOK(ID3D11Device, device, CreatePixelShader, 15);
    INSTALL_VTABLE_HOOK(ID3D11DeviceContext, deviceContext, PSSetShader, 9);
}