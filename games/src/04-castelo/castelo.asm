; =============================================================================
; castelo.asm - "A Fuga do Castelo Corvoalto"
; Aventura de ação vista de cima para TK90X/TK95 (ZX Spectrum 48K).
; Jogo original: mapa, desenhos, personagens, textos e sons próprios.
;
; Licença MIT - (c) 2026 Projeto Emulador TK90X
;
; Montagem: ./build.sh  (gera gerado/castelo.tap e copia para games/)
; Este arquivo contém só o que é deste jogo; o motor fica em motor/engine.asm.
; =============================================================================

        DEVICE ZXSPECTRUM48

ORIGEM  EQU 32768

; ---------------- carregador BASIC: 10 CLEAR 32767:LOAD ""CODE:RANDOMIZE USR 32768
        ORG 0xF000
basic:
        DB 0, 10
        DW linha_fim - linha_ini
linha_ini:
        DB 0xFD                         ; CLEAR
        DB "32767", 0x0E, 0, 0
        DW ORIGEM-1
        DB 0
        DB ':', 0xEF, '"', '"', 0xAF    ; :LOAD ""CODE
        DB ':', 0xF9, 0xC0              ; :RANDOMIZE USR
        DB "32768", 0x0E, 0, 0
        DW ORIGEM
        DB 0
        DB 0x0D
linha_fim:
basic_fim:

; ---------------- código ----------------
        ORG ORIGEM
inicio:
        include "motor/engine.asm"
        include "gerado/graficos.inc"
        include "gerado/mapa.inc"

; ---------------- cores ----------------
C_PRETO     EQU 0
C_AZUL      EQU 1
C_VERMELHO  EQU 2
C_MAGENTA   EQU 3
C_VERDE     EQU 4
C_CIANO     EQU 5
C_AMARELO   EQU 6
C_BRANCO    EQU 7
BRILHO      EQU 0x40
PISCA       EQU 0x80

; ---------------- regras ----------------
ENERGY_MAX      EQU 128
DRAIN_TICKS     EQU 40          ; 1 ponto de energia a cada 1,6 s (25 passos/s)
LIVES           EQU 3
RESPAWN_TICKS   EQU 75          ; 3 s (+ até 2,5 s) para o monstro voltar
PIECES_NEEDED   EQU 3
PROJ_RANGE      EQU 30
WIN_BONUS       EQU 0x1000      ; pontos (BCD) por escapar

; ---------------- gráficos usados pelo motor ----------------
PLAYER_SPR      EQU SPR_LIA_CIMA0
PLAYER_INK      EQU BRILHO|C_CIANO
PROJ_SPR8       EQU SPR8_FAISCA0
PROJ_INK        EQU BRILHO|C_AMARELO
POOF_SPR        EQU SPR_PUF0
POOF_INK        EQU BRILHO|C_BRANCO
GFX_WALLS       EQU TILE_parede0
GFX_DOOR        EQU TILE_porta
GFX_MASTER      EQU TILE_mestra
GFX_UP          EQU TILE_sobe
GFX_DOWN        EQU TILE_desce
GFX_DECO1       EQU TILE_pilar
GFX_DECO2       EQU TILE_caixote
MASTER_ATTR     EQU PISCA|BRILHO|C_MAGENTA
ICON_KEY        EQU CHR_ic_chave
ICON_PIECE      EQU CHR_ic_peca
ICON_EMPTY      EQU CHR_ic_vazio

; portas trancadas: vermelha, verde, azul, amarela
door_attr:
        DB BRILHO|(C_VERMELHO<<3), BRILHO|(C_VERDE<<3), BRILHO|(C_CIANO<<3), BRILHO|(C_AMARELO<<3)
key_icon_attr:
        DB BRILHO|C_VERMELHO, BRILHO|C_VERDE, BRILHO|C_CIANO, BRILHO|C_AMARELO

; ---------------- inimigos ----------------
; sprite, tinta, comportamento (0 fantasma, 1 voo quicando, 2 perseguidor),
; máscara de velocidade (anda quando passo AND máscara = 0), vida, dano, pontos (BCD)
NUM_ETYPES      EQU 3
enemy_types:
        DB SPR_FANTASMA0,  BRILHO|C_BRANCO,   0, 1, 1, 12
        DW 0x0050
        DB SPR_MORCEGO0,   BRILHO|C_MAGENTA,  1, 0, 1, 8
        DW 0x0030
        DB SPR_CAVALEIRO0, BRILHO|C_AMARELO,  2, 1, 2, 16
        DW 0x0100

; ---------------- objetos ----------------
; efeito (0 chave, 1 pedaço da Chave Mestra, 2 comida), valor (cor da chave /
; energia), tinta, 0, desenho, pontos (BCD)
item_kinds:
        DB 0, 0, BRILHO|C_VERMELHO, 0
        DW ITEM_chave, 0x0100
        DB 0, 1, BRILHO|C_VERDE, 0
        DW ITEM_chave, 0x0100
        DB 0, 2, BRILHO|C_CIANO, 0
        DW ITEM_chave, 0x0100
        DB 0, 3, BRILHO|C_AMARELO, 0
        DW ITEM_chave, 0x0100
        DB 1, 0, PISCA|BRILHO|C_MAGENTA, 0
        DW ITEM_peca, 0x0500
        DB 2, 40, BRILHO|C_BRANCO, 0
        DW ITEM_frango, 0x0050
        DB 2, 24, BRILHO|C_VERMELHO, 0
        DW ITEM_maca, 0x0050
        DB 2, 32, C_AMARELO, 0
        DW ITEM_pao, 0x0050

; ---------------- painel ----------------
HUD_ATTR        EQU BRILHO|C_BRANCO
HUD_NAME_ATTR   EQU BRILHO|(C_AZUL<<3)|C_AMARELO
HUD_FLOOR_ATTR  EQU BRILHO|C_CIANO
BAR_ATTR_OK     EQU BRILHO|C_VERDE
BAR_ATTR_LOW    EQU PISCA|BRILHO|C_VERMELHO
PIECE_ATTR      EQU BRILHO|C_MAGENTA
EMPTY_ATTR      EQU C_BRANCO

hud_labels:
        DB 2,HUD_ATTR
        DB 1,21,0,"ENERGIA", 1,21,25,"VIDAS"
        DB 1,22,0,"PONTOS",  1,22,15,"CHAVES"
        DB 1,23,0,"MESTRA"
        DB 0

; ---------------- telas ----------------
TITLE_ATTR          EQU C_BRANCO
TITLE_PAGE_ROW      EQU 5
TITLE_PAGE_ROWS     EQU 13
TITLE_PAGE_FRAMES   EQU 450
END_SCORE_ROW       EQU 16
END_SCORE_COL       EQU 17

game_title:
        DB 2,BRILHO|C_CIANO, 1,0,11, "A FUGA DO"
        DB 3, 2,BRILHO|C_AMARELO, 1,1,7, "CASTELO CORVOALTO", 4
        DB 2,C_VERMELHO, 1,3,0, "================================"
        DB 2,PISCA|BRILHO|C_BRANCO, 1,20,1, "ESPAÇO, 0 ou ENTER para jogar"
        DB 2,BRILHO|C_VERDE, 1,22,5, "Projeto Emulador TK90X"
        DB 2,C_VERDE, 1,23,2, "Licença MIT - jogo original"
        DB 0

game_page1:
        DB 2,BRILHO|C_BRANCO, 1,5,0
        DB "Lia, aprendiz de alquimista, foi",13
        DB "trancada no Castelo Corvoalto",13
        DB "pelo terrível Barão Sombrio.",13
        DB 13
        DB 2,BRILHO|C_AMARELO
        DB "O portão principal só abre com a",13
        DB "Chave Mestra, que o Barão partiu",13
        DB "em 3 pedaços e escondeu entre o",13
        DB "porão e o andar superior.",13
        DB 2,BRILHO|C_CIANO
        DB "Fantasmas, morcegos e cavaleiros",13
        DB "esqueletos rondam os salões. Use",13
        DB "faíscas mágicas, ache as chaves",13
        DB "coloridas e coma o que achar:",13
        DB "a energia acaba depressa!",0

game_page2:
        DB 2,BRILHO|C_AMARELO, 1,5,11, "CONTROLES"
        DB 2,BRILHO|C_BRANCO, 1,6,0
        DB "Q cima  A baixo  O esq.  P dir.",13
        DB "ESPAÇO ou 0: lança faísca",13
        DB "Joystick Sinclair: 6 7 8 9 e 0",13
        DB "ou Cursor: 5 6 7 8 e 0",13
        DB 2,C_BRANCO, 1,12,1, "Lia", 1,12,6, "Fantasma", 1,12,15, "Morcego", 1,12,23, "Cavaleiro"
        DB 2,BRILHO|C_VERDE, 1,14,0
        DB "Chave colorida abre a porta da",13
        DB "mesma cor. Comida repõe energia.",13
        DB "Junte os 3 pedaços e fuja pelo",13
        DB "Portão Principal!",0

; desenha os personagens na página 2 (linhas 10-11)
game_page2_hook:
        ld hl,vitrine
.l:     ld a,(hl)
        cp 0xFF
        ret z
        push hl
        call spr16_ptr
        ex de,hl
        pop hl
        inc hl
        ld c,(hl)
        inc hl
        ld a,(hl)
        inc hl
        push hl
        push af
        ex de,hl
        ld b,80
        xor a
        push bc
        call draw_spr
        pop bc
        pop af                      ; A = tinta
        ld e,a
        ld a,c                      ; coluna = x/8
        rrca
        rrca
        rrca
        and 31
        ld l,a
        ld h,0
        ld bc,ATTRS+10*32
        add hl,bc
        ld (hl),e
        inc hl
        ld (hl),e
        ld bc,31
        add hl,bc
        ld (hl),e
        inc hl
        ld (hl),e
        pop hl
        jr .l
vitrine:
        DB SPR_LIA_BAIXO0, 8, PLAYER_INK
        DB SPR_FANTASMA0, 72, BRILHO|C_BRANCO
        DB SPR_MORCEGO0, 136, BRILHO|C_MAGENTA
        DB SPR_CAVALEIRO0, 208, BRILHO|C_AMARELO
        DB 0xFF

txt_mode0:
        DB 2,BRILHO|C_CIANO, 1,19,1, "Teclas 1/2 joystick: SINCLAIR",0
txt_mode1:
        DB 2,BRILHO|C_CIANO, 1,19,1, "Teclas 1/2 joystick: CURSOR  ",0

txt_victory:
        DB 3, 2,BRILHO|C_AMARELO, 1,2,11, "LIBERDADE!", 4
        DB 2,BRILHO|C_BRANCO, 1,6,0
        DB "Lia atravessa o Portão Principal",13
        DB "com a Chave Mestra inteira e",13
        DB "foge do Castelo Corvoalto sob a",13
        DB "luz da lua. O Barão Sombrio",13
        DB "nunca mais vai prendê-la!",13
        DB 2,BRILHO|C_VERDE, 1,16,9, "PONTOS:"
        DB 2,BRILHO|C_CIANO, 1,21,4, "Tecle algo para continuar"
        DB 2,BRILHO|C_AMARELO
        DB 0

txt_gameover:
        DB 3, 2,BRILHO|C_VERMELHO, 1,2,10, "FIM DE JOGO", 4
        DB 2,BRILHO|C_BRANCO, 1,6,1
        DB "A energia de Lia acabou e os",13
        DB "monstros do Barão Sombrio a",13
        DB "levaram de volta para a cela.",13
        DB 13
        DB "Tente outra vez: a Chave Mestra",13
        DB "continua à espera!",13
        DB 2,BRILHO|C_VERDE, 1,16,9, "PONTOS:"
        DB 2,BRILHO|C_CIANO, 1,21,4, "Tecle algo para continuar"
        DB 2,BRILHO|C_AMARELO
        DB 0

fim:
        ASSERT fim <= SHBUF16

        EMPTYTAP "gerado/castelo.tap"
        SAVETAP  "gerado/castelo.tap", BASIC, "Corvoalto", basic, basic_fim - basic, 10
        SAVETAP  "gerado/castelo.tap", CODE,  "corvoalto", inicio, fim - inicio
