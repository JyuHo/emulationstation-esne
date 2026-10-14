#!/usr/bin/env python3
"""Génère EsneEmbeddedKeys.h: clés développeur intégrées au binaire (ES-NE).

usage: esne_embed_keys.py SORTIE.h [keys.conf]

Format de keys.conf: NOM=valeur par ligne ('#' commentaire). Sans fichier, la table est vide.
Les valeurs sont masquées par un XOR avec un masque aléatoire: cela évite qu'un `strings` les affiche,
mais ne les protège pas d'une personne qui analyse le binaire. Ne mets que des clés que tu acceptes de rendre publiques.
"""
import os
import secrets
import sys

KNOWN = ("SCREENSCRAPER_SOFTNAME", "SCREENSCRAPER_DEV_LOGIN", "GAMESDB_APIKEY", "HFS_DEV_LOGIN", "CHEEVOS_DEV_LOGIN")


def parse(path):
    keys = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            name, value = (part.strip() for part in line.split("=", 1))
            if len(value) >= 2 and value[0] in "\"'" and value[-1] == value[0]:
                value = value[1:-1]
            if name in KNOWN and value:
                keys[name] = value
            elif name and name not in KNOWN:
                print(f"esne_embed_keys: nom inconnu ignoré: {name}", file=sys.stderr)
    return keys


def c_array(data):
    return ", ".join(str(b) for b in data) or "0"


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = argv[1]
    keys = parse(argv[2]) if len(argv) > 2 and argv[2] else {}
    pad = secrets.token_bytes(32)
    lines = ["// Généré par tools/esne_embed_keys.py: ne pas committer.", "#pragma once", "#include <cstddef>", "namespace EsneKeys { namespace embedded {",
             f"static const unsigned char pad[] = {{{c_array(pad)}}};",
             "struct Entry { const char* name; const unsigned char* data; size_t size; };"]
    entries = []
    for i, (name, value) in enumerate(sorted(keys.items())):
        raw = value.encode("utf-8")
        masked = bytes(b ^ pad[j % len(pad)] for j, b in enumerate(raw))
        lines.append(f"static const unsigned char v{i}[] = {{{c_array(masked)}}};")
        entries.append(f'{{"{name}", v{i}, {len(raw)}}}')
    entries.append("{nullptr, nullptr, 0}")
    lines.append("static const Entry entries[] = {" + ", ".join(entries) + "};")
    lines.append("} }")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    os.chmod(out, 0o600)
    print(f"esne_embed_keys: {len(keys)} clé(s) intégrée(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
