# 開発用の確認（Linux／WSL など、g++ がある環境で）：
# src/share.cpp の SafeRelPath（受け取る側の守り）を取り出して、決まった例で試す。
#   python3 tests/test_safe_relpath.py
import pathlib, subprocess, sys, tempfile
src = (pathlib.Path(__file__).parent.parent / 'src' / 'share.cpp').read_text(encoding='utf-8')
i = src.index('bool SafeRelPath(const std::wstring& p) {'); j = src.index('\n}\n', i) + 3
cases = [('教材\\\\第1回\\\\練習.docx', 1), ('配布.pdf', 1), ('E ドライブ\\\\a\\\\b.txt', 1),
         ('..\\\\Windows\\\\evil.exe', 0), ('教材\\\\..\\\\..\\\\evil.exe', 0), ('C:\\\\Windows\\\\evil.exe', 0),
         ('\\\\\\\\server\\\\share\\\\x', 0), ('\\\\abs.txt', 0), ('a/../b', 0), ('a\\\\.\\\\b', 0),
         ('a\\\\b. ', 0), ('a\\\\b.', 0), ('x:y', 0), ('', 0), ('a\\\\\\\\b', 0)]
body = ''.join('{L"%s",%d},' % c for c in cases)
code = '#include <string>\n#include <cstdio>\n' + src[i:j] + '''
int main(){ struct T{const wchar_t* p; int want;} t[]={%s}; int bad=0;
  for(auto&x:t){ if((int)SafeRelPath(x.p)!=x.want) bad++; }
  printf("%%d 件中 %%d 件が想定と違う\\n",(int)(sizeof(t)/sizeof(t[0])),bad); return bad; }''' % body
d = tempfile.mkdtemp(); c = pathlib.Path(d) / 't.cpp'; c.write_text(code, encoding='utf-8')
subprocess.check_call(['g++', '-std=c++20', '-o', str(pathlib.Path(d) / 't'), str(c)])
sys.exit(subprocess.call([str(pathlib.Path(d) / 't')]))
