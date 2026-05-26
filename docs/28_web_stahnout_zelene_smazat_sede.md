# Web: zelené odkazy Stáhnout a šedé Smazat u chráněných souborů

Úprava seznamu souborů:

- odkazy ve sloupci `Stáhnout` jsou zvýrazněné zelenou barvou
- při najetí myší se zelená zesvětlí
- u chráněného souboru se ve sloupci `Smazat` zobrazuje pouze šedý text `smazat`
- funkce ochrany se nemění; chráněný soubor stále nejde smazat ani přímým URL požadavkem

CSS třídy jsou v souboru:

```text
components/web_server/web_server.c
```

Důležité části:

```css
.download-cell a{color:#7fe08a;font-weight:bold;}
.download-cell a:hover{color:#a6f5ad;}
```
