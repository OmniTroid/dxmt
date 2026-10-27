#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "-d")) {
    FILE *f = fopen(argv[2], "rb");
    if (!f) return 3;
    std::vector<char> code;
    char b[4096];
    size_t m;
    while ((m = fread(b, 1, sizeof(b), f)) > 0) code.insert(code.end(), b, b + m);
    fclose(f);
    ID3DBlob *text = nullptr;
    if (FAILED(D3DDisassemble(code.data(), code.size(), 0, nullptr, &text))) return 1;
    printf("%s\n", (const char *)text->GetBufferPointer());
    return 0;
  }
  if (argc < 5) {
    fprintf(stderr, "usage: hlslc <file.hlsl> <entry> <target> <out>\n");
    return 2;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 3;
  std::vector<char> src;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) src.insert(src.end(), buf, buf + n);
  fclose(f);
  ID3DBlob *code = nullptr, *err = nullptr;
  HRESULT hr = D3DCompile(src.data(), src.size(), argv[1], nullptr, nullptr, argv[2], argv[3], 0, 0, &code, &err);
  if (err) fprintf(stderr, "%.*s\n", (int)err->GetBufferSize(), (const char *)err->GetBufferPointer());
  if (FAILED(hr)) return 1;
  FILE *o = fopen(argv[4], "wb");
  fwrite(code->GetBufferPointer(), 1, code->GetBufferSize(), o);
  fclose(o);
  return 0;
}
