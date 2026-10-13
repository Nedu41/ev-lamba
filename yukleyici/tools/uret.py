"""cihazlar.json'dan manifests/<id>.json uretir (ESP Web Tools manifestleri) ve firmware/ dosyalarini dogrular.
Kullanim:  python tools/uret.py
Yeni imaj eklemek: dosyayi firmware/ altina koy, cihazlar.json'a girdi ekle, bu betigi calistir.
"""
import json
import os
import sys

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    veri = json.load(open(os.path.join(KOK, 'cihazlar.json'), encoding='utf-8'))
    os.makedirs(os.path.join(KOK, 'manifests'), exist_ok=True)
    eksik = []
    for c in veri['cihazlar']:
        yol = os.path.join(KOK, 'firmware', c['dosya'])
        if not os.path.exists(yol):
            eksik.append(c['dosya'])
        man = {
            'name': c['ad'],
            'version': c.get('surum', ''),
            'new_install_prompt_erase': True,
            'builds': [{
                'chipFamily': c['cip'],
                'parts': [{'path': '../firmware/' + c['dosya'], 'offset': 0}],
            }],
        }
        json.dump(man, open(os.path.join(KOK, 'manifests', c['id'] + '.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    # dosya:// ile acilinca fetch engellenir: veriyi script olarak da yaz
    with open(os.path.join(KOK, 'cihazlar.js'), 'w', encoding='utf-8') as f:
        f.write('window.CIHAZLAR = ' + json.dumps(veri, ensure_ascii=False, indent=1) + ';\n')
    print(len(veri['cihazlar']), 'manifest uretildi')
    if eksik:
        print('EKSIK imaj dosyalari:', ', '.join(eksik))
        sys.exit(1)


if __name__ == '__main__':
    main()
