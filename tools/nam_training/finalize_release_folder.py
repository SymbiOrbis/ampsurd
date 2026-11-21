# One-off tool (2026-11-21): renames the owner's _Final release folder and adds name / maker / legal notice metadata.
# Usage: python3 finalize_release_folder.py <dir containing _Final> <output dir>
import sys,os,re,json,zipfile,shutil,io,xml.etree.ElementTree as ET
# One-off tool (2026-11-21): renames the owner's _Final release folder and adds name / maker / legal notice metadata.
# Usage: python3 finalize_release_folder.py <dir containing _Final> <output dir>
IN,OUT=sys.argv[1:3]
# One-off tool (2026-11-21): renames the owner's _Final release folder and adds name / maker / legal notice metadata.
# Usage: python3 finalize_release_folder.py <dir containing _Final> <output dir>
AMPS={ # old folder -> (new folder, full name, maker, model, amp file prefix in old names)
 'Ampsurd Amp Capture Mesa Dual Rectifier 100W Head':('Ampsurd Amp Capture Mesa Dual Rectifier 100 W Head','Mesa Dual Rectifier','Mesa/Boogie','Dual Rectifier 100 W Head','Dual '),
 'Ampsurd Amp Capture Framus Cobra 100 W':('Ampsurd Amp Capture Framus Cobra 100 W','Framus Cobra','Framus','Cobra 100 W','Framus Cobra '),
 'Ampsurd Amp Capture EVH 5150 III 100 W 6L6':('Ampsurd Amp Capture EVH 5150 III 100 W 6L6','EVH 5150 III','EVH','5150 III 100 W 6L6','EVH 5150 III '),
}
def notice(maker,gear):
  return (f"Neural captures made by AMPSURD (Radovan Glovňa) from a privately owned {maker} {gear}. "
   f"All product names, trademarks and brands belong to their respective owners and are used only to identify the equipment that was captured. "
   f"AMPSURD is not affiliated with, sponsored by or endorsed by {maker}, and the captures were made without the manufacturer's involvement or approval. "
   f"They represent the sound of one individual unit and are provided as they are, without any warranty. "
   f"Terms of use: you may use these captures to play, record, perform and publish your own music, including commercial releases, without paying any royalties or further fees to AMPSURD. "
   f"You may not sell, rent or otherwise distribute the capture files themselves, alone or as part of another product, without written permission from AMPSURD.")
def fix_nam(data,name,maker,model,gtype,note):
  d=json.loads(data); m=d.setdefault('metadata',{})
  m['name']=name; m['modeled_by']='AMPSURD (Radovan Glovňa)'; m['gear_type']=gtype; m['gear_make']=maker; m['gear_model']=model
  m['notice']=note
  return json.dumps(d).encode()
F=os.path.join(IN,'_Final'); O=os.path.join(OUT,'_Final'); os.makedirs(O)
shutil.copy2(os.path.join(F,"999. Artists' tone pick - Radovan Glovna (The Black Death Ensemble).ampsurdpack"),O)
count={'amp':0,'nam':0}
for old,(new,full,maker,model,pre) in AMPS.items():
  src=os.path.join(F,old); dst=os.path.join(O,new); os.makedirs(dst); note=notice(maker,model)
  for fn in sorted(os.listdir(src)):
    p=os.path.join(src,fn)
    if fn.endswith('.ampsurdamp'):
      if ' old' in fn: continue
      chmode=fn[:-11][len(pre):]; disp=f"{full} {chmode}"
      zi=zipfile.ZipFile(p); buf=io.BytesIO(); zo=zipfile.ZipFile(buf,'w',zipfile.ZIP_DEFLATED)
      xml=zi.read('amp.xml').decode()
      knob={g.get('file'):float(g.get('knob')) for g in ET.fromstring(xml).iter('GAIN')}
      xml2=re.sub(r'(<AMPSURD_AMP\b[^>]*?)\bname="[^"]*"', lambda mm: mm.group(1)+f'name="{disp}"', xml, count=1)
      esc=note.replace('&','&amp;').replace('"','&quot;')
      xml2=xml2.replace('<AMPSURD_AMP ',f'<AMPSURD_AMP maker="{maker}" model="{model}" notice="{esc}" ',1)
      for it in zi.infolist():
        b=zi.read(it.filename)
        if it.filename=='amp.xml': b=xml2.encode()
        elif it.filename.endswith('.nam'):
          hh,mm=divmod(round((7+knob[it.filename])*60),60)
          b=fix_nam(b,f"{disp} GAIN {hh:02d}{mm:02d}",maker,model,'amp',note)
        zo.writestr(it.filename,b)
      zo.close(); open(os.path.join(dst,f"Ampsurd Amp Capture {disp}.ampsurdamp"),'wb').write(buf.getvalue()); count['amp']+=1
    elif os.path.isdir(p):   # "<full> NAMs"
      for root,_,files in os.walk(p):
        rel=os.path.relpath(root,src); os.makedirs(os.path.join(dst,rel),exist_ok=True)
        for f in files:
          if not f.endswith('.nam'): continue
          b=fix_nam(open(os.path.join(root,f),'rb').read(),f[:-4],maker,model,'amp',note)
          open(os.path.join(dst,rel,f),'wb').write(b); count['nam']+=1
  open(os.path.join(dst,'NOTICE.txt'),'w',encoding='utf-8',newline='\r\n').write(
    f"{full} - AMPSURD amp captures\n\n"+note.replace('. ','.\n')+"\n\nContact: https://ampsurd.com · contact@ampsurd.com\n")
pd=os.path.join(O,'Pedals'); os.makedirs(pd); pn=notice('Gerhat','Screamer pedal')
for old,new in [('Screamer standard.nam','Gerhat Screamer STANDARD'),('Screamer halfdrivew.nam','Gerhat Screamer HALF DRIVE')]:
  open(os.path.join(pd,new+'.nam'),'wb').write(fix_nam(open(os.path.join(F,'Pedals',old),'rb').read(),new,'Gerhat','Screamer','pedal',pn)); count['nam']+=1
open(os.path.join(pd,'NOTICE.txt'),'w',encoding='utf-8',newline='\r\n').write("Gerhat Screamer - AMPSURD pedal captures\n\n"+pn.replace('. ','.\n')+"\n\nContact: https://ampsurd.com · contact@ampsurd.com\n")
print(count)
