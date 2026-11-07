"""Embed selected public roots from the locally installed Mozilla/certifi store."""
from pathlib import Path
import os
import re
import ssl
import tempfile
import certifi

names = {'GTS Root R4', 'ISRG Root X1', 'Amazon Root CA 1',
         'DigiCert Global Root G2', 'USERTrust RSA Certification Authority',
         'Sectigo Public Server Authentication Root R46'}
selected = {}
for pem in re.findall(r'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',
                      Path(certifi.where()).read_text(), re.S):
    fd, name = tempfile.mkstemp()
    try:
        with os.fdopen(fd, 'w') as file:
            file.write(pem)
        subject = ssl._ssl._test_decode_cert(name)['subject']
        common_name = next((value for part in subject for key,value in part if key == 'commonName'), None)
        if common_name in names:
            selected[common_name] = pem
    finally:
        os.unlink(name)
assert set(selected) == names, names - set(selected)
bundle = '\n'.join(selected[name] for name in sorted(selected)) + '\n'
root = Path(__file__).resolve().parents[1]
(root / 'src/tls_roots.h').write_text(
    '#pragma once\n// Public roots from Mozilla/certifi; no private material.\n'
    '// ' + ', '.join(sorted(selected)) + '\n'
    'static const char kPublicApiRoots[] = R"PEM(' + bundle + ')PEM";\n')
print('Embedded six public TLS roots, including current mempool.space trust chain')
