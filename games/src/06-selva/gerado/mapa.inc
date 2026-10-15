; Gerado por mapa.py (motor/mapa_lib.py) - nao edite.

NUM_ROOMS   EQU 30
NUM_LINKS   EQU 36
NUM_ITEMS   EQU 27
START_ROOM  EQU 21
START_X     EQU 120
START_Y     EQU 72

; registro da sala (21 bytes): nome, andar, attr parede, attr chão, attr decoração,
; estilo de parede, modelo, N(dest,link) S O L, escada(dest,cx,cy,tile), inimigo(tipo,qtd)
rooms:
        DB 0,0,68,7,68,0,7,255,0,6,17,255,0,255,0,255,0,0,0,1,3   ; 0 NINHO DOS ZUNZUNS
        DB 1,0,4,7,68,1,10,255,0,7,13,255,0,2,14,255,0,0,0,0,2   ; 1 BOSQUE DOS CIPÓS
        DB 2,0,68,7,70,0,5,255,0,255,0,1,14,3,15,25,14,8,7,2,2   ; 2 ESCADARIA VERDE
        DB 3,4,70,7,70,3,12,254,35,9,18,2,15,255,0,255,0,0,0,128,3   ; 3 TEMPLO DAS MIL RAÍZES
        DB 4,4,6,7,70,3,11,255,0,10,20,255,0,5,21,255,0,0,0,2,2   ; 4 PÁTIO DOS ÍDOLOS
        DB 5,4,70,7,7,0,11,255,0,255,0,4,21,255,0,255,0,0,0,130,3   ; 5 TORRE TOMBADA
        DB 6,0,68,7,68,0,1,0,17,255,0,255,0,7,16,255,0,0,0,0,3   ; 6 CLAREIRA DOS ECOS
        DB 7,0,4,7,68,1,6,1,13,255,0,6,16,8,12,255,0,0,0,1,2   ; 7 TRILHA SINUOSA
        DB 8,0,68,7,68,0,3,255,0,14,10,7,12,255,0,255,0,0,0,3,2   ; 8 RAÍZES GÊMEAS
        DB 9,4,70,7,70,3,8,3,18,15,9,255,0,10,19,255,0,0,0,2,2   ; 9 PORTAL DE PEDRA
        DB 10,4,4,7,70,1,11,4,20,255,0,9,19,11,22,255,0,0,0,0,2   ; 10 GALERIA DE MUSGO
        DB 11,4,6,7,7,3,12,255,0,17,23,10,22,255,0,255,0,0,0,129,3   ; 11 COLUNAS CAÍDAS
        DB 12,1,4,7,6,1,13,255,0,18,25,255,0,13,24,27,6,4,8,0,2   ; 12 LAGOA ESCURA
        DB 13,1,4,7,6,1,9,255,0,255,0,12,24,14,11,255,0,0,0,0,3   ; 13 BREJO DAS NÉVOAS
        DB 14,2,6,7,6,3,8,8,10,255,0,13,11,15,8,255,0,0,0,1,2   ; 14 PONTE DE TRONCOS
        DB 15,2,68,7,68,0,5,9,9,21,0,14,8,255,0,255,0,0,0,2,1   ; 15 ENCRUZILHADA
        DB 16,3,66,7,68,2,4,255,0,22,29,255,0,17,30,255,0,0,0,3,2   ; 16 CAMPO DE ESPINHOS
        DB 17,3,2,7,68,2,14,11,23,255,0,16,30,255,0,255,0,0,0,130,3   ; 17 ESPINHEIRO RUBRO
        DB 18,1,4,7,6,1,2,12,25,24,28,255,0,19,26,255,0,0,0,1,2   ; 18 CHARCO VERDE
        DB 19,1,6,7,6,3,9,255,0,25,27,18,26,255,0,255,0,0,0,131,3   ; 19 ILHA DE JUNCOS
        DB 20,2,68,7,68,0,6,255,0,26,4,255,0,21,3,255,0,0,0,1,2   ; 20 MATA DOS SUSSURROS
        DB 21,2,68,7,68,0,1,15,0,27,1,20,3,22,2,255,0,0,0,0,1   ; 21 CLAREIRA DO BALÃO
        DB 22,3,66,7,68,0,9,16,29,28,32,21,2,23,31,255,0,0,0,1,2   ; 22 VALE DAS FLORES
        DB 23,3,2,7,6,2,7,255,0,29,34,22,31,255,0,255,0,0,0,2,2   ; 23 TOCA DO CASCO
        DB 24,1,4,7,6,1,14,18,28,255,0,255,0,255,0,255,0,0,0,128,3   ; 24 POÇO DE LAMA
        DB 25,1,68,7,68,0,10,19,27,255,0,255,0,26,5,2,14,8,8,3,2   ; 25 ÁRVORE OCA
        DB 26,2,4,7,68,1,2,20,4,255,0,25,5,27,6,255,0,0,0,1,2   ; 26 SAMAMBAIAL
        DB 27,2,68,7,68,0,3,21,1,255,0,26,6,28,7,12,14,8,7,0,2   ; 27 TRILHA DO RIACHO
        DB 28,3,66,7,68,2,2,22,32,255,0,27,7,29,33,255,0,0,0,3,3   ; 28 JARDIM CARNÍVORO
        DB 29,3,2,7,6,2,4,23,34,255,0,28,33,255,0,255,0,0,0,130,3   ; 29 FIM DA TRILHA

room_names:
        DW rname_0,rname_1,rname_2,rname_3,rname_4,rname_5,rname_6,rname_7,rname_8,rname_9,rname_10,rname_11,rname_12,rname_13,rname_14,rname_15,rname_16,rname_17,rname_18,rname_19,rname_20,rname_21,rname_22,rname_23,rname_24,rname_25,rname_26,rname_27,rname_28,rname_29
rname_0: DB "NINHO DOS ZUNZUNS",0
rname_1: DB "BOSQUE DOS CIPÓS",0
rname_2: DB "ESCADARIA VERDE",0
rname_3: DB "TEMPLO DAS MIL RAÍZES",0
rname_4: DB "PÁTIO DOS ÍDOLOS",0
rname_5: DB "TORRE TOMBADA",0
rname_6: DB "CLAREIRA DOS ECOS",0
rname_7: DB "TRILHA SINUOSA",0
rname_8: DB "RAÍZES GÊMEAS",0
rname_9: DB "PORTAL DE PEDRA",0
rname_10: DB "GALERIA DE MUSGO",0
rname_11: DB "COLUNAS CAÍDAS",0
rname_12: DB "LAGOA ESCURA",0
rname_13: DB "BREJO DAS NÉVOAS",0
rname_14: DB "PONTE DE TRONCOS",0
rname_15: DB "ENCRUZILHADA",0
rname_16: DB "CAMPO DE ESPINHOS",0
rname_17: DB "ESPINHEIRO RUBRO",0
rname_18: DB "CHARCO VERDE",0
rname_19: DB "ILHA DE JUNCOS",0
rname_20: DB "MATA DOS SUSSURROS",0
rname_21: DB "CLAREIRA DO BALÃO",0
rname_22: DB "VALE DAS FLORES",0
rname_23: DB "TOCA DO CASCO",0
rname_24: DB "POÇO DE LAMA",0
rname_25: DB "ÁRVORE OCA",0
rname_26: DB "SAMAMBAIAL",0
rname_27: DB "TRILHA DO RIACHO",0
rname_28: DB "JARDIM CARNÍVORO",0
rname_29: DB "FIM DA TRILHA",0
floor_names:
        DW fname_0,fname_1,fname_2,fname_3,fname_4
fname_0: DB "MATA ALTA",0
fname_1: DB "BREJO",0
fname_2: DB "MATA FECHADA",0
fname_3: DB "ESPINHAIS",0
fname_4: DB "RUÍNAS",0

templates:
        DW tmpl_0,tmpl_1,tmpl_2,tmpl_3,tmpl_4,tmpl_5,tmpl_6,tmpl_7,tmpl_8,tmpl_9,tmpl_10,tmpl_11,tmpl_12,tmpl_13,tmpl_14
tmpl_0: DB 0   ; vazio
tmpl_1: DB 8   ; clareira
        DB 2,2,4,2,1
        DB 26,2,4,2,1
        DB 2,4,4,2,1
        DB 26,4,4,2,1
        DB 2,14,4,2,1
        DB 26,14,4,2,1
        DB 2,16,4,2,1
        DB 26,16,4,2,1
tmpl_2: DB 12   ; moitas
        DB 4,4,4,2,1
        DB 12,4,4,2,1
        DB 20,4,4,2,1
        DB 4,6,4,2,1
        DB 12,6,4,2,1
        DB 20,6,4,2,1
        DB 8,12,4,2,1
        DB 16,12,4,2,1
        DB 24,12,4,2,1
        DB 8,14,4,2,1
        DB 16,14,4,2,1
        DB 24,14,4,2,1
tmpl_3: DB 8   ; anel
        DB 6,6,8,2,1
        DB 18,6,8,2,1
        DB 6,8,8,2,1
        DB 18,8,8,2,1
        DB 6,10,8,2,1
        DB 18,10,8,2,1
        DB 6,12,8,2,1
        DB 18,12,8,2,1
tmpl_4: DB 16   ; zigue
        DB 6,2,4,2,1
        DB 22,2,4,2,1
        DB 6,4,4,2,1
        DB 22,4,4,2,1
        DB 6,6,4,2,1
        DB 14,6,4,2,1
        DB 22,6,4,2,1
        DB 6,8,4,2,1
        DB 14,8,4,2,1
        DB 22,8,4,2,1
        DB 14,10,4,2,1
        DB 14,12,4,2,1
        DB 2,14,8,2,1
        DB 22,14,8,2,1
        DB 2,16,8,2,1
        DB 22,16,8,2,1
tmpl_5: DB 12   ; corredores
        DB 2,2,8,2,1
        DB 22,2,8,2,1
        DB 2,4,8,2,1
        DB 22,4,8,2,1
        DB 14,6,4,2,1
        DB 14,8,4,2,1
        DB 14,10,4,2,1
        DB 14,12,4,2,1
        DB 2,14,8,2,1
        DB 22,14,8,2,1
        DB 2,16,8,2,1
        DB 22,16,8,2,1
tmpl_6: DB 12   ; serpente
        DB 6,2,4,2,1
        DB 6,4,4,2,1
        DB 6,6,4,2,1
        DB 14,6,12,2,1
        DB 6,8,4,2,1
        DB 14,8,12,2,1
        DB 14,10,4,2,1
        DB 14,12,4,2,1
        DB 2,14,8,2,1
        DB 22,14,4,2,1
        DB 2,16,8,2,1
        DB 22,16,4,2,1
tmpl_7: DB 14   ; bolsoes
        DB 10,2,4,2,1
        DB 18,2,4,2,1
        DB 10,4,4,2,1
        DB 18,4,4,2,1
        DB 6,10,4,2,1
        DB 14,10,4,2,1
        DB 22,10,4,2,1
        DB 6,12,4,2,1
        DB 14,12,4,2,1
        DB 22,12,4,2,1
        DB 6,14,4,2,1
        DB 22,14,4,2,1
        DB 6,16,4,2,1
        DB 22,16,4,2,1
tmpl_8: DB 8   ; trilha
        DB 2,2,12,2,1
        DB 18,2,12,2,1
        DB 2,4,12,2,1
        DB 18,4,12,2,1
        DB 2,14,12,2,1
        DB 18,14,12,2,1
        DB 2,16,12,2,1
        DB 18,16,12,2,1
tmpl_9: DB 8   ; ilhas
        DB 10,6,4,2,1
        DB 18,6,4,2,1
        DB 10,8,4,2,1
        DB 18,8,4,2,1
        DB 6,10,4,2,1
        DB 22,10,4,2,1
        DB 6,12,4,2,1
        DB 22,12,4,2,1
tmpl_10: DB 14   ; arvoredo
        DB 6,2,4,2,9
        DB 22,2,4,2,9
        DB 6,4,4,2,9
        DB 22,4,4,2,9
        DB 14,6,4,2,9
        DB 14,8,4,2,9
        DB 6,10,4,2,9
        DB 22,10,4,2,9
        DB 6,12,4,2,9
        DB 22,12,4,2,9
        DB 2,14,4,2,9
        DB 26,14,4,2,9
        DB 2,16,4,2,9
        DB 26,16,4,2,9
tmpl_11: DB 16   ; ruinas
        DB 2,2,4,2,10
        DB 26,2,4,2,10
        DB 2,4,4,2,10
        DB 26,4,4,2,10
        DB 10,6,4,2,10
        DB 18,6,4,2,10
        DB 10,8,4,2,10
        DB 18,8,4,2,10
        DB 10,10,4,2,10
        DB 18,10,4,2,10
        DB 10,12,4,2,10
        DB 18,12,4,2,10
        DB 2,14,4,2,10
        DB 26,14,4,2,10
        DB 2,16,4,2,10
        DB 26,16,4,2,10
tmpl_12: DB 16   ; templo
        DB 2,2,4,2,9
        DB 10,2,4,2,10
        DB 18,2,4,2,10
        DB 26,2,4,2,9
        DB 2,4,4,2,9
        DB 10,4,4,2,10
        DB 18,4,4,2,10
        DB 26,4,4,2,9
        DB 2,14,4,2,10
        DB 10,14,4,2,9
        DB 18,14,4,2,9
        DB 26,14,4,2,10
        DB 2,16,4,2,10
        DB 10,16,4,2,9
        DB 18,16,4,2,9
        DB 26,16,4,2,10
tmpl_13: DB 9   ; brejo
        DB 6,2,2,2,1
        DB 22,2,2,2,1
        DB 12,6,2,2,1
        DB 22,6,2,2,1
        DB 8,10,2,2,1
        DB 20,10,2,2,1
        DB 4,14,2,2,1
        DB 18,14,2,2,1
        DB 26,14,2,2,1
tmpl_14: DB 14   ; galhos
        DB 2,2,10,2,1
        DB 20,2,10,2,1
        DB 2,4,10,2,1
        DB 20,4,10,2,1
        DB 6,8,4,2,1
        DB 14,8,4,2,1
        DB 22,8,4,2,1
        DB 6,10,4,2,1
        DB 14,10,4,2,1
        DB 22,10,4,2,1
        DB 2,14,8,2,1
        DB 22,14,8,2,1
        DB 2,16,8,2,1
        DB 22,16,8,2,1

links_lock:
        DB 0,0,2,0,0,0,0,3,0,0,0,0,0,0,0,0,4,0,0,3,0,0,0,3,0,0,0,0,2,0,0,0,0,0,4,6

; objetos: sala, cx, cy, tipo
items_def:
        DB 0,3,3,3   ; NINHO DOS ZUNZUNS
        DB 5,15,7,3   ; TORRE TOMBADA
        DB 24,15,13,3   ; POÇO DE LAMA
        DB 29,27,11,3   ; FIM DA TRILHA
        DB 25,7,15,0   ; ÁRVORE OCA
        DB 17,27,7,1   ; ESPINHEIRO RUBRO
        DB 11,15,11,2   ; COLUNAS CAÍDAS
        DB 21,7,11,4   ; CLAREIRA DO BALÃO
        DB 8,15,3,5   ; RAÍZES GÊMEAS
        DB 13,3,3,6   ; BREJO DAS NÉVOAS
        DB 18,27,3,4   ; CHARCO VERDE
        DB 16,27,3,5   ; CAMPO DE ESPINHOS
        DB 4,7,15,6   ; PÁTIO DOS ÍDOLOS
        DB 1,19,11,4   ; BOSQUE DOS CIPÓS
        DB 23,3,15,6   ; TOCA DO CASCO
        DB 27,3,3,5   ; TRILHA DO RIACHO
        DB 6,15,11,4   ; CLAREIRA DOS ECOS
        DB 28,3,2,6   ; JARDIM CARNÍVORO
        DB 21,23,7,7   ; CLAREIRA DO BALÃO
        DB 20,27,3,8   ; MATA DOS SUSSURROS
        DB 15,19,15,9   ; ENCRUZILHADA
        DB 22,15,7,8   ; VALE DAS FLORES
        DB 7,19,3,7   ; TRILHA SINUOSA
        DB 12,23,11,9   ; LAGOA ESCURA
        DB 9,15,11,7   ; PORTAL DE PEDRA
        DB 28,27,16,9   ; JARDIM CARNÍVORO
        DB 5,23,15,8   ; TORRE TOMBADA
