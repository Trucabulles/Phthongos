"""Validate FR/EN catalogs and explicit UI localization markers (stdlib only)."""
from pathlib import Path
import json,re,sys
root=Path(__file__).resolve().parents[1]
def unique(pairs):
 result={}
 for key,value in pairs:
  if key in result:raise ValueError('Duplicate translation key: '+key)
  result[key]=value
 return result
fr=json.loads((root/'translations/fr.json').read_text(encoding='utf-8-sig'),object_pairs_hook=unique)
en=json.loads((root/'translations/en.json').read_text(encoding='utf-8-sig'),object_pairs_hook=unique)
errors=[]
if fr.keys()!=en.keys():errors.append('FR/EN key sets differ')
for key,source in fr.items():
 target=en.get(key)
 if not isinstance(source,str) or not isinstance(target,str) or not source or not target:errors.append('Empty or invalid message: '+key);continue
 if set(re.findall(r'\{\d+\}',source))!=set(re.findall(r'\{\d+\}',target)):errors.append('Placeholders differ: '+key)
 if ')phtloc"' in key+source+target:errors.append('Unsupported C++ raw-string delimiter: '+key)
pattern=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|R"(?P<delim>[^ ()\\\t\r\n]*)\([\s\S]*?\)(?P=delim)"|(?P<string>"(?:\\.|[^"\\])*"(?:\s*"(?:\\.|[^"\\])*")*)')
for path in sorted((root/'native').glob('*.*')):
 if path.name in ('Tests.cpp','LocalisationTests.cpp','Localisation.cpp','Localisation.h','SpectralPalettes.h','ReferenceIcons.h') or path.suffix not in ('.h','.cpp'):continue
 code=path.read_text(encoding='utf-8-sig')
 arrays=[(m.start(),m.end()) for m in re.finditer(r'const\s+char\s*\*\s*\w+\[\][^;]*;',code)]
 for match in pattern.finditer(code):
  if not match.group('string'):continue
  try:value=''.join(json.loads(t) for t in re.findall(r'"(?:\\.|[^"\\])*"',match.group('string')))
  except ValueError:continue
  if any(a<=match.start()<b for a,b in arrays):continue # roots/scales are localized when inserted; mode IDs stay raw.
  prefix=code[max(0,match.start()-100):match.start()]
  marked=bool(re.search(r'(?:tr|localisation::format|localisation::count)\(\s*(?:(?:juce::)?String::fromUTF8\(\s*)?$',prefix))
  counted=bool(re.search(r'localisation::count\(\s*$',prefix))
  if marked and not (value in fr or (counted and value+".one" in fr and value+".other" in fr)):errors.append(f'{path.relative_to(root)}:{code.count(chr(10),0,match.start())+1}: missing catalog entry {value!r}')
  if not marked and value!='Français' and (value in en and en[value]!=value):errors.append(f'{path.relative_to(root)}:{code.count(chr(10),0,match.start())+1}: untranslated UI literal {value!r}')
if errors:
 print('\n'.join(errors));sys.exit(1)
print(f'FR/EN catalogs: {len(fr)} entries, matching placeholders, explicit UI markers checked.')
