; Gerado por mapa.py (motor/mapa_lib.py) - nao edite.

NUM_ROOMS   EQU 24
NUM_LINKS   EQU 27
NUM_ITEMS   EQU 19
START_ROOM  EQU 1
START_X     EQU 120
START_Y     EQU 72

; registro da sala (21 bytes): nome, andar, attr parede, attr chão, attr decoração,
; estilo de parede, modelo, N(dest,link) S O L, escada(dest,cx,cy,tile), inimigo(tipo,qtd)
rooms:
        DB 0,0,7,7,71,1,1,1,1,254,6,3,4,5,5,255,0,0,0,0,1   ; 0 PORTÃO PRINCIPAL
        DB 1,0,66,7,71,0,2,2,0,0,1,7,2,9,3,255,0,0,0,1,1   ; 1 SALÃO DE ENTRADA
        DB 2,0,70,7,67,1,9,255,0,1,0,6,12,8,11,255,0,0,0,2,2   ; 2 SALA DO TRONO
        DB 3,0,2,7,6,0,3,255,0,255,0,4,7,0,4,255,0,0,0,1,2   ; 3 COZINHA
        DB 4,0,6,7,6,2,4,11,8,255,0,255,0,3,7,18,24,10,8,1,2   ; 4 DESPENSA
        DB 5,0,67,7,6,0,3,9,9,255,0,0,5,255,0,255,0,0,0,0,2   ; 5 REFEITÓRIO
        DB 6,0,71,7,6,1,10,255,0,255,0,10,13,2,12,255,0,0,0,0,3   ; 6 CAPELA
        DB 7,0,3,7,71,2,1,255,0,255,0,255,0,1,2,13,14,8,7,128,2   ; 7 GALERIA DE RETRATOS
        DB 8,0,5,7,69,1,2,255,0,9,10,2,11,255,0,23,14,8,8,2,2   ; 8 SALA DE ARMAS
        DB 9,0,66,7,71,0,6,8,10,5,9,1,3,255,0,255,0,0,0,1,2   ; 9 CORREDOR LESTE
        DB 10,0,4,7,71,0,5,255,0,11,14,255,0,6,13,255,0,0,0,2,2   ; 10 CORREDOR OESTE
        DB 11,0,68,7,4,1,8,10,14,4,8,255,0,255,0,255,0,0,0,129,3   ; 11 JARDIM DE INVERNO
        DB 12,1,6,7,66,2,11,255,0,13,15,17,18,16,19,255,0,0,0,0,2   ; 12 BIBLIOTECA
        DB 13,1,67,7,6,2,4,12,15,255,0,15,16,14,17,7,14,8,8,1,2   ; 13 ESCRITÓRIO DO BARÃO
        DB 14,1,66,7,71,0,1,255,0,255,0,13,17,255,0,255,0,0,0,130,3   ; 14 QUARTO DO BARÃO
        DB 15,1,69,7,70,2,2,17,20,255,0,255,0,13,16,255,0,0,0,0,2   ; 15 SALA DE MÚSICA
        DB 16,1,68,7,69,1,7,255,0,255,0,12,19,255,0,255,0,0,0,1,3   ; 16 LABORATÓRIO
        DB 17,1,7,7,71,1,5,255,0,15,20,255,0,12,18,255,0,0,0,2,2   ; 17 TORRE NORTE
        DB 18,2,6,79,78,3,4,20,22,255,0,255,0,19,21,4,6,8,7,1,2   ; 18 ADEGA
        DB 19,2,7,79,79,1,1,22,24,255,0,18,21,21,23,255,0,0,0,2,2   ; 19 MASMORRA
        DB 20,2,3,79,79,3,7,255,0,18,22,255,0,22,25,255,0,0,0,0,3   ; 20 CRIPTA
        DB 21,2,5,79,79,3,6,255,0,255,0,19,23,255,0,255,0,0,0,128,3   ; 21 POÇO ANTIGO
        DB 22,2,4,79,79,3,5,255,0,19,24,20,25,23,26,255,0,0,0,130,3   ; 22 CATACUMBAS
        DB 23,2,2,79,78,1,1,255,0,255,0,22,26,255,0,8,14,8,7,1,3   ; 23 SALA DAS CORRENTES

room_names:
        DW rname_0,rname_1,rname_2,rname_3,rname_4,rname_5,rname_6,rname_7,rname_8,rname_9,rname_10,rname_11,rname_12,rname_13,rname_14,rname_15,rname_16,rname_17,rname_18,rname_19,rname_20,rname_21,rname_22,rname_23
rname_0: DB "PORTÃO PRINCIPAL",0
rname_1: DB "SALÃO DE ENTRADA",0
rname_2: DB "SALA DO TRONO",0
rname_3: DB "COZINHA",0
rname_4: DB "DESPENSA",0
rname_5: DB "REFEITÓRIO",0
rname_6: DB "CAPELA",0
rname_7: DB "GALERIA DE RETRATOS",0
rname_8: DB "SALA DE ARMAS",0
rname_9: DB "CORREDOR LESTE",0
rname_10: DB "CORREDOR OESTE",0
rname_11: DB "JARDIM DE INVERNO",0
rname_12: DB "BIBLIOTECA",0
rname_13: DB "ESCRITÓRIO DO BARÃO",0
rname_14: DB "QUARTO DO BARÃO",0
rname_15: DB "SALA DE MÚSICA",0
rname_16: DB "LABORATÓRIO",0
rname_17: DB "TORRE NORTE",0
rname_18: DB "ADEGA",0
rname_19: DB "MASMORRA",0
rname_20: DB "CRIPTA",0
rname_21: DB "POÇO ANTIGO",0
rname_22: DB "CATACUMBAS",0
rname_23: DB "SALA DAS CORRENTES",0
floor_names:
        DW fname_0,fname_1,fname_2
fname_0: DB "TÉRREO",0
fname_1: DB "ANDAR SUPERIOR",0
fname_2: DB "PORÃO",0

templates:
        DW tmpl_0,tmpl_1,tmpl_2,tmpl_3,tmpl_4,tmpl_5,tmpl_6,tmpl_7,tmpl_8,tmpl_9,tmpl_10,tmpl_11,tmpl_12
tmpl_0: DB 0   ; vazio
tmpl_1: DB 4   ; pilares4
        DB 6,4,2,2,9
        DB 24,4,2,2,9
        DB 6,14,2,2,9
        DB 24,14,2,2,9
tmpl_2: DB 8   ; colunata
        DB 6,6,2,2,9
        DB 10,6,2,2,9
        DB 20,6,2,2,9
        DB 24,6,2,2,9
        DB 6,12,2,2,9
        DB 10,12,2,2,9
        DB 20,12,2,2,9
        DB 24,12,2,2,9
tmpl_3: DB 1   ; mesa
        DB 10,8,12,4,10
tmpl_4: DB 5   ; caixotes
        DB 4,4,4,2,10
        DB 24,4,2,4,10
        DB 4,12,2,4,10
        DB 20,14,6,2,10
        DB 10,12,2,2,10
tmpl_5: DB 4   ; cruz
        DB 2,2,8,4,1
        DB 22,2,8,4,1
        DB 2,14,8,4,1
        DB 22,14,8,4,1
tmpl_6: DB 1   ; bloco
        DB 12,6,8,8,1
tmpl_7: DB 5   ; tumulos
        DB 6,6,4,2,10
        DB 22,6,4,2,10
        DB 6,12,4,2,10
        DB 22,12,4,2,10
        DB 14,8,4,4,9
tmpl_8: DB 8   ; jardim
        DB 6,4,2,2,9
        DB 12,4,2,2,9
        DB 18,4,2,2,9
        DB 24,4,2,2,9
        DB 6,14,2,2,9
        DB 12,14,2,2,9
        DB 18,14,2,2,9
        DB 24,14,2,2,9
tmpl_9: DB 5   ; trono
        DB 8,4,2,2,9
        DB 22,4,2,2,9
        DB 8,12,2,2,9
        DB 22,12,2,2,9
        DB 14,4,4,2,10
tmpl_10: DB 4   ; capela
        DB 6,8,6,2,10
        DB 20,8,6,2,10
        DB 6,12,6,2,10
        DB 20,12,6,2,10
tmpl_11: DB 5   ; estantes
        DB 4,4,8,2,10
        DB 20,4,8,2,10
        DB 4,14,8,2,10
        DB 20,14,8,2,10
        DB 12,8,8,4,10
tmpl_12: DB 2   ; corredor_h
        DB 2,2,28,4,1
        DB 2,14,28,4,1

links_lock:
        DB 0,0,2,0,0,0,6,0,3,0,0,0,5,0,0,0,0,5,0,0,0,0,0,4,0,0,0

; objetos: sala, cx, cy, tipo
items_def:
        DB 20,26,4,0   ; CRIPTA
        DB 12,14,4,1   ; BIBLIOTECA
        DB 6,14,4,2   ; CAPELA
        DB 17,14,8,3   ; TORRE NORTE
        DB 21,4,4,4   ; POÇO ANTIGO
        DB 14,26,14,4   ; QUARTO DO BARÃO
        DB 11,14,8,4   ; JARDIM DE INVERNO
        DB 3,6,8,5   ; COZINHA
        DB 5,14,4,7   ; REFEITÓRIO
        DB 4,6,8,6   ; DESPENSA
        DB 18,22,10,7   ; ADEGA
        DB 19,24,8,6   ; MASMORRA
        DB 22,14,8,5   ; CATACUMBAS
        DB 15,14,4,6   ; SALA DE MÚSICA
        DB 16,26,14,5   ; LABORATÓRIO
        DB 8,4,4,7   ; SALA DE ARMAS
        DB 10,14,8,6   ; CORREDOR OESTE
        DB 2,4,14,5   ; SALA DO TRONO
        DB 9,4,4,6   ; CORREDOR LESTE
