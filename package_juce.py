"""Local review package: explicit resources only; never user data, optional weights or test binaries."""
from pathlib import Path
import json,shutil,datetime,hashlib,argparse
from tools.legal_resources import prepare as prepare_legal_resources
from tools.audit_licenses import audit as audit_licenses
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--configuration",choices=["Release","Debug"],default="Release")
parser.add_argument("--dev",action="store_true",help="Prepare a stable local runtime under .run; never distribute it")
parser.add_argument("--portable",action="store_true",help="Keep preferences and optional downloads beside the application")
options=parser.parse_args()
legal_manifest=prepare_legal_resources()
if options.configuration=="Debug" and not options.dev:parser.error("Debug is for local development only; use --dev")
root=Path(__file__).resolve().parent
stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
destination=root/'.run'/options.configuration if options.dev else root/'releases'/('Phthongos-JUCE-'+stamp)
source=root/'livraison'
exe=root/'native-build/PhthongosJUCE_artefacts'/options.configuration/'Phthongos.exe'
assert exe.is_file(), 'Compile JUCE first'
required_files=['msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll','runtime/manifest.json','runtime/phthongos_muscriptor.dll','runtime/research_speex.dll','player-assets/gmPrograms.json','player-assets/logo.png','player-assets/msghq.json','player-assets/piano.sf2','player-assets/fluidsynth/libfluidsynth-3.dll','player-assets/fluidsynth/sndfile.dll']
assert all((source/file).is_file() for file in required_files), 'A required runtime resource is missing'
manual_files=['documentation/manuel/Phthongos_Manuel_utilisateur.pdf','documentation/manuel/Phthongos_User_Manual.pdf']
assert all((root/file).is_file() for file in manual_files), 'A required PDF user manual is missing'
destination.mkdir(parents=True,exist_ok=options.dev)
if options.portable:
 (destination/'portable.flag').write_text('Portable profile: adjacent data directory.\n',encoding='utf8')
shutil.copy2(exe,destination/'Phthongos.exe')
for file in required_files:
 target=destination/file;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/file,target)
def ignore(path,names):
 if Path(path).name=='third-party':return [name for name in names if name in ('javascript','installer','rust','rustysynth','sources')]
 if Path(path).name=='sources':return [name for name in names if name.endswith('.crate') or name.startswith('wix')]
 return []
shutil.copytree(source/'third-party',destination/'third-party',ignore=ignore,dirs_exist_ok=options.dev)
bp_source=root/'models/basic-pitch'
ort_source=root/'vendor/onnxruntime'
bp_manifest=json.loads((bp_source/'SOURCE.json').read_text(encoding='utf8'))
ort_manifest=json.loads((ort_source/'SOURCE.json').read_text(encoding='utf8'))
assert hashlib.sha256((bp_source/'nmp.onnx').read_bytes()).hexdigest()==bp_manifest['sha256']
for name,expected in ort_manifest['runtime_files'].items():
 assert hashlib.sha256((ort_source/'bin'/name).read_bytes()).hexdigest()==expected
 shutil.copy2(ort_source/'bin'/name,destination/'runtime'/name)
bp_target=destination/'runtime/basic-pitch';bp_target.mkdir(parents=True,exist_ok=options.dev)
shutil.copy2(bp_source/'nmp.onnx',bp_target/'nmp.onnx')
(bp_target/'manifest.json').write_text(json.dumps({'model_sha256':bp_manifest['sha256'],'runtime_sha256':ort_manifest['runtime_files']['onnxruntime.dll'],'onnxruntime_version':ort_manifest['version'],'decode_version':1},indent=2),encoding='utf8')
for component,folder,files in [('basic-pitch',bp_source,['LICENSE','NOTICE','SOURCE.json']),('onnxruntime',ort_source,['LICENSE','ThirdPartyNotices.txt','SOURCE.json'])]:
 notice=destination/'third-party'/component;notice.mkdir(parents=True,exist_ok=options.dev)
 for name in files:shutil.copy2(folder/name,notice/name)
juce=destination/'third-party/juce';juce.mkdir(exist_ok=True)
shutil.copy2(root/'vendor/JUCE/LICENSE.md',juce/'LICENSE.md')
(juce/'SOURCE.txt').write_text('JUCE 9.0.4. Source utilisee : vendor/JUCE dans le fork local.\nLicence JUCE originale jointe sans modification.\n',encoding='utf8')
manifest=legal_manifest
manifest['rust']=[]
manifest['native_core']=True
manifest['basic_pitch']=bp_manifest
manifest['onnxruntime']=ort_manifest
shutil.copytree(root/'vendor/lzma-native',destination/'third-party/lzma-native',dirs_exist_ok=options.dev)
manifest['xz']={'version':'5.2','source':'lzma-native/xz-5.2','notices':['lzma-native/xz-5.2/COPYING']}
shutil.copytree(root/'vendor/midi-bpm',destination/'third-party/midi-bpm',dirs_exist_ok=options.dev)
manifest['midi_tempo']={'name':'MIDI BPM Detection — C++ port','revision':'e14a86c1c15a3523c458c19549ce7be3f9576734','license':'MIT','notices':['midi-bpm/LICENSE.txt','midi-bpm/SOURCE.md'],'source':'https://github.com/valsteen/midi_bpm_detection'}
manifest['javascript']=[]
manifest['native_ui']={'name':'JUCE','version':'9.0.4','notices':['juce/LICENSE.md'],'source':'vendor/JUCE'}
manifest['colormaps']={'names':['Inferno','Magma','Viridis'],'license':'CC0-1.0','notices':['colormaps/NOTICE.txt','colormaps/LICENSE.txt'],'source':'https://github.com/BIDS/colormap'}
manifest['note']='Fork JUCE : aucune interface Tauri, React ou WebView2 embarquee.'
for name in ['LICENSE','LICENSE_PHTHONGOS.md','LICENSE_PHTHONGOS_EN.md']:
 shutil.copy2(root/name,destination/name)
own=destination/'third-party/phthongos';own.mkdir(exist_ok=options.dev)
for name in ['LICENSE_PHTHONGOS.md','LICENSE_PHTHONGOS_EN.md']:
 shutil.copy2(root/name,own/name)
(destination/'third-party/LISEZ-MOI.txt').write_text("Phthongos — Trucabulles\nApplication license: LICENSE (English translation); LICENSE_PHTHONGOS.md (French reference).\nThird-party components retain their own licenses. Offline FR/EN credits and complete notices are available in the application.\ncomponents.json inventories the native package only. Optional model weights and the optional HQ bank are not bundled.\nFluidSynth and libsndfile are unmodified, replaceable DLLs. Their verified corresponding-source archives and codec notices are retained alongside them.\nThe MuScriptor engine code and optional neural weights have separate licenses. No optional GGUF weights are included.\n",encoding='utf8')
credits=root/'documentation/credits'
for name in ['credits-fr.txt','credits-en.txt']:
 shutil.copy2(credits/name,destination/'third-party'/name)
optional_notices=destination/'third-party/optional-models';optional_notices.mkdir(exist_ok=options.dev)
for name in ['MuScriptor-CC-BY-NC-4.0.txt','MuScriptor-model-card.md']:
 shutil.copy2(credits/name,optional_notices/name)
manifest['optional_models']={'bundled':False,'source':'https://huggingface.co/DamRsn/muscriptor-gguf','revision':'d7045f94e8b19427f4ff9542975035e66596e51c','license':'CC-BY-NC-4.0','notices':['optional-models/MuScriptor-CC-BY-NC-4.0.txt','optional-models/MuScriptor-model-card.md']}
manifest['bundled'].extend([
 {'name':'Basic Pitch','license':'Apache-2.0','binary':'runtime/basic-pitch/nmp.onnx','sha256':bp_manifest['sha256'],'notices':['basic-pitch/LICENSE','basic-pitch/NOTICE','basic-pitch/SOURCE.json']},
 {'name':'ONNX Runtime','version':ort_manifest['version'],'license':'MIT','notices':['onnxruntime/LICENSE','onnxruntime/ThirdPartyNotices.txt','onnxruntime/SOURCE.json']}
])
(destination/'third-party/components.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
for file in ['MANUEL.txt','MANUEL-PHTHONGOS.md','MANUAL-EN.md']:
 if (root/file).is_file():shutil.copy2(root/file,destination/file)
for file in manual_files:
 target=destination/file;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/file,target)
runtime_manifest={'onnxruntime_version':ort_manifest['version'],'files':{name:hashlib.sha256((destination/'runtime'/name).read_bytes()).hexdigest() for name in ['onnxruntime.dll','onnxruntime_providers_shared.dll','research_speex.dll','phthongos_muscriptor.dll']}}
(destination/'runtime/manifest.json').write_text(json.dumps(runtime_manifest,indent=2),encoding='utf8')
files={str(p.relative_to(destination)):hashlib.sha256(p.read_bytes()).hexdigest() for p in destination.rglob('*') if p.is_file() and (not options.dev or p.relative_to(destination).parts[0] not in ('data','test-results')) and p.name!='manifest-sha256.json'}
assert not any(name.lower().endswith(('.gguf','.pht','.mid')) or (name.lower().endswith('.onnx') and name.replace('\\','/')!='runtime/basic-pitch/nmp.onnx') for name in files)
(destination/'manifest-sha256.json').write_text(json.dumps(files,indent=2),encoding='utf8')
audit=audit_licenses(destination)
report=root/'build/legal-audio'/(destination.name+'-audit.json')
report.parent.mkdir(parents=True,exist_ok=True)
report.write_text(json.dumps(audit,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
if not audit['passed']:
 raise ValueError('Package licensing checks failed; see '+str(report))
print(destination)
