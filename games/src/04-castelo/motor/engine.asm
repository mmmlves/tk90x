; =============================================================================
; engine.asm - motor LABIRINTO: aventura de ação vista de cima, com troca de
; tela por salas (flip-screen), para TK90X/TK95 (ZX Spectrum 48K).
;
; Licença MIT - (c) 2026 Projeto Emulador TK90X
;
; O motor não sabe nada do jogo: mapa, gráficos, textos e tabelas vêm do
; arquivo do jogo (ver castelo.asm e LEIAME.md, seção "Motor"). Rótulos e
; constantes que o jogo precisa fornecer:
;   dados do mapa (mapa_lib.py): rooms room_names floor_names templates
;     links_lock items_def NUM_ROOMS NUM_LINKS NUM_ITEMS START_ROOM START_X START_Y
;   gráficos (gfx2asm.py): spr16_src spr8_src NUM_SPR16 NUM_SPR8
;   ladrilhos: GFX_WALLS (4 estilos seguidos) GFX_DOOR GFX_MASTER GFX_UP
;     GFX_DOWN GFX_DECO1 GFX_DECO2; door_attr (4 bytes) MASTER_ATTR
;   jogador/projétil/poeira: PLAYER_SPR PLAYER_INK PROJ_SPR8 PROJ_INK
;     POOF_SPR POOF_INK PROJ_RANGE
;   tabelas: enemy_types (8 bytes cada) NUM_ETYPES; item_kinds (8 bytes cada)
;   painel: hud_labels HUD_ATTR HUD_NAME_ATTR HUD_FLOOR_ATTR BAR_ATTR_OK
;     BAR_ATTR_LOW ICON_KEY ICON_PIECE ICON_EMPTY PIECE_ATTR EMPTY_ATTR
;     key_icon_attr (4 bytes) PIECES_NEEDED
;   regras: ENERGY_MAX (<=128) DRAIN_TICKS LIVES RESPAWN_TICKS WIN_BONUS (BCD)
;   telas: TITLE_ATTR game_title game_page1 game_page2 game_page2_hook
;     txt_mode0 txt_mode1 TITLE_PAGE_ROW TITLE_PAGE_ROWS TITLE_PAGE_FRAMES
;     txt_victory txt_gameover END_SCORE_ROW END_SCORE_COL
; =============================================================================

; ---------------- endereços fixos ----------------
FRAMES      EQU 23672           ; contador de quadros da ROM (byte baixo)
SCREEN      EQU 0x4000
ATTRS       EQU 0x5800
SHBUF16     EQU 0xC000          ; sprites 16x16 pré-deslocados (192 bytes cada)
SHBUF8      EQU 0xD400          ; sprites 8x8 pré-deslocados (64 bytes cada)
CMAP        EQU 0xD800          ; mapa de células da sala (32x20)
BGATTR      EQU 0xDB00          ; atributos de fundo da sala (32x20)
ROWTAB      EQU 0xDE00          ; endereço de tela de cada linha de pixel (192 x 2)
GLYPHBUF    EQU 0xDF80          ; 8 bytes: caractere sendo montado
ENBUF       EQU 0xDF90          ; 16 bytes: barra de energia

; ---------------- ladrilhos (valores no CMAP) ----------------
T_FLOOR     EQU 0
T_WALL      EQU 1
T_DOOR      EQU 2               ; 2..5 = portas vermelha, verde, azul, amarela
T_MASTER    EQU 6               ; porta da Chave Mestra
T_UP        EQU 7
T_DOWN      EQU 8
T_DECO1     EQU 9
T_DECO2     EQU 10

; ---------------- registro da sala (gerado por mapa_lib.py) ----------------
R_NAME      EQU 0
R_FLOOR     EQU 1
R_WATTR     EQU 2
R_FATTR     EQU 3
R_DATTR     EQU 4
R_STYLE     EQU 5
R_TMPL      EQU 6
R_EXITS     EQU 7               ; N(dest,link) S(dest,link) O(dest,link) L(dest,link)
R_SDEST     EQU 15
R_SX        EQU 16
R_SY        EQU 17
R_STYPE     EQU 18
R_ETYPE     EQU 19
R_ECOUNT    EQU 20
ROOM_SIZE   EQU 21
EXIT_OUT    EQU 0xFE            ; destino = fora do castelo (vitória)
NO_EXIT     EQU 0xFF

; ---------------- objetos na tela ----------------
O_VIS       EQU 0               ; estado desejado
O_X         EQU 1
O_Y         EQU 2
O_GFX       EQU 3               ; 2 bytes: quadro (deslocamento 0)
O_SIZE      EQU 5               ; 0 = 16x16, 1 = 8x8
O_INK       EQU 6
O_OVIS      EQU 7               ; o que está desenhado agora (XOR)
O_OX        EQU 8
O_OY        EQU 9
O_OGFX      EQU 10
O_OSIZE     EQU 12
O_AVIS      EQU 13              ; caixa de atributos pintada agora
O_AX        EQU 14
O_AY        EQU 15
O_ASIZE     EQU 16
O_STATE     EQU 17
O_TYPE      EQU 18
O_DX        EQU 19
O_DY        EQU 20
O_TIMER     EQU 21
O_HP        EQU 22
O_DTIM      EQU 23
OBJ_SIZE    EQU 24
MAX_EN      EQU 4
NUM_OBJ     EQU 2 + MAX_EN      ; jogador, projétil, inimigos

; estados do inimigo
ES_OFF      EQU 0
ES_WAIT     EQU 1
ES_APPEAR   EQU 2
ES_ALIVE    EQU 3
ES_DYING    EQU 4

; bits de entrada
IN_R        EQU 0
IN_L        EQU 1
IN_D        EQU 2
IN_U        EQU 3
IN_F        EQU 4

MAX_LINKS   EQU 64
MAX_ITEMS   EQU 64

; =============================================================================
; Início
; =============================================================================
engine_start:
        di
        ld sp,0x8000
        ei
        xor a
        out (0xFE),a
        ld (ctrl_mode),a
        call init_rowtab
        call preshift
.main:
        call title_screen
        call game_run
        jr .main

; -----------------------------------------------------------------------------
; ROWTAB[y] = endereço do primeiro byte da linha de pixel y
init_rowtab:
        ld hl,ROWTAB
        ld b,0
.l:     ld a,b
        and 0x07
        ld d,a
        ld a,b
        and 0xC0
        rrca
        rrca
        rrca
        or d
        or 0x40
        ld d,a
        ld a,b
        and 0x38
        rlca
        rlca
        ld (hl),a
        inc hl
        ld (hl),d
        inc hl
        inc b
        ld a,b
        cp 192
        jr nz,.l
        ret

; A = linha de pixel -> HL = endereço na tela (coluna 0). Preserva BC, DE.
row_addr:
        push de
        ld l,a
        ld h,0
        add hl,hl
        ld de,ROWTAB
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ex de,hl
        pop de
        ret

; -----------------------------------------------------------------------------
; Pré-desloca os sprites em 0, 2, 4 e 6 pixels (movimento de 2 em 2 pixels).
preshift:
        ld hl,spr16_src
        ld de,SHBUF16
        ld c,NUM_SPR16
.spr:   push bc
        xor a
        ld (sh_cnt),a
.sh:    push hl
        ld c,16
.row:   push bc
        ld b,(hl)
        inc hl
        ld c,(hl)
        inc hl
        push hl
        ld l,0
        ld a,(sh_cnt)
        or a
        jr z,.st
.sl:    srl b
        rr c
        rr l
        dec a
        jr nz,.sl
.st:    ld a,b
        ld (de),a
        inc de
        ld a,c
        ld (de),a
        inc de
        ld a,l
        ld (de),a
        inc de
        pop hl
        pop bc
        dec c
        jr nz,.row
        pop hl
        ld a,(sh_cnt)
        add a,2
        ld (sh_cnt),a
        cp 8
        jr nz,.sh
        ld bc,32
        add hl,bc
        pop bc
        dec c
        jr nz,.spr
        ; sprites 8x8
        ld hl,spr8_src
        ld de,SHBUF8
        ld c,NUM_SPR8
.s8:    push bc
        xor a
        ld (sh_cnt),a
.h8:    push hl
        ld c,8
.r8:    ld b,(hl)
        inc hl
        push hl
        ld l,0
        ld a,(sh_cnt)
        or a
        jr z,.t8
.l8:    srl b
        rr l
        dec a
        jr nz,.l8
.t8:    ld a,b
        ld (de),a
        inc de
        ld a,l
        ld (de),a
        inc de
        pop hl
        dec c
        jr nz,.r8
        pop hl
        ld a,(sh_cnt)
        add a,2
        ld (sh_cnt),a
        cp 8
        jr nz,.h8
        ld bc,8
        add hl,bc
        pop bc
        dec c
        jr nz,.s8
        ret

; A = índice -> HL = SHBUF16 + A*192
spr16_ptr:
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld d,h
        ld e,l
        add hl,hl
        add hl,de
        ld de,SHBUF16
        add hl,de
        ret

; A = índice -> HL = SHBUF8 + A*64
spr8_ptr:
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,SHBUF8
        add hl,de
        ret

; =============================================================================
; Tela, texto e som
; =============================================================================
; A = atributo: limpa a tela inteira
cls:
        push af
        ld hl,SCREEN
        ld de,SCREEN+1
        ld bc,6143
        ld (hl),0
        ldir
        pop af
        ld hl,ATTRS
        ld de,ATTRS+1
        ld bc,767
        ld (hl),a
        ldir
        ret

; B = primeira linha de caracteres, C = quantas, A = atributo
clear_rows:
        ld (cr_attr),a
.r:     push bc
        ld a,b
        add a,a
        add a,a
        add a,a
        ld c,8
.l:     push af
        call row_addr
        ld d,h
        ld e,l
        inc de
        ld (hl),0
        push bc
        ld bc,31
        ldir
        pop bc
        pop af
        inc a
        dec c
        jr nz,.l
        ld l,b
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,ATTRS
        add hl,de
        ld a,(cr_attr)
        ld (hl),a
        ld d,h
        ld e,l
        inc de
        ld bc,31
        ldir
        pop bc
        inc b
        dec c
        jr nz,.r
        ret

; Imprime a cadeia em HL. Códigos: 0 fim, 1 lin col (posição), 2 attr,
; 3 altura dupla, 4 altura normal, 13 nova linha. Aceita UTF-8 de U+00C0 a
; U+00FF (acentos do português), montando o acento sobre a letra da ROM.
print:
.l:     ld a,(hl)
        inc hl
        or a
        ret z
        cp 1
        jr z,.at
        cp 2
        jr z,.attr
        cp 3
        jr z,.dbl
        cp 4
        jr z,.sgl
        cp 13
        jr z,.nl
        cp 0xC3
        jr z,.utf
        cp 0x80
        jr nc,.l
        push hl
        call putc
        pop hl
        jr .l
.at:    ld a,(hl)
        inc hl
        ld (prow),a
        ld a,(hl)
        inc hl
        ld (pcol),a
        ld (pmargin),a
        jr .l
.attr:  ld a,(hl)
        inc hl
        ld (pattr),a
        jr .l
.dbl:   ld a,1
        ld (pdbl),a
        jr .l
.sgl:   xor a
        ld (pdbl),a
        jr .l
.nl:    ld a,(pmargin)
        ld (pcol),a
        ld a,(pdbl)
        inc a
        ld b,a
        ld a,(prow)
        add a,b
        ld (prow),a
        jr .l
.utf:   ld a,(hl)
        inc hl
        push hl
        call putc_latin
        pop hl
        jr .l

; A = caractere ASCII 32..127
putc:
        call rom_glyph
        ld de,GLYPHBUF
        ld bc,8
        ldir
        jp draw_glyph

; A = caractere -> HL = desenho na fonte da ROM
rom_glyph:
        sub 32
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        ld bc,0x3D00
        add hl,bc
        ret

; A = segundo byte do UTF-8 (0x80..0xBF, depois de 0xC3)
putc_latin:
        sub 0x80
        and 0x3F
        add a,a
        ld l,a
        ld h,0
        ld bc,acctab
        add hl,bc
        ld a,(hl)
        inc hl
        ld c,(hl)
        ld (acc_base),a
        ld a,c
        ld (acc_type),a
        ld a,(acc_base)
        call rom_glyph
        ld de,GLYPHBUF
        ld bc,8
        ldir
        ld a,(acc_type)
        or a
        jp z,draw_glyph
        cp 5
        jr nz,.top
        ld hl,GLYPHBUF+7            ; cedilha
        ld a,(hl)
        or 0x10
        ld (hl),a
        jp draw_glyph
.top:   ld a,(acc_base)
        cp 'A'
        jr c,.acc
        cp 'Z'+1
        jr nc,.acc
        ld hl,GLYPHBUF+6            ; maiúscula: desce a letra 1 linha
        ld de,GLYPHBUF+7
        ld bc,7
        lddr
.acc:   ld a,(acc_type)
        dec a
        add a,a
        ld l,a
        ld h,0
        ld bc,acc_pat
        add hl,bc
        ld a,(hl)
        ld (GLYPHBUF),a
        inc hl
        ld a,(hl)
        ld (GLYPHBUF+1),a
        jp draw_glyph

; Desenha GLYPHBUF em (prow,pcol) com pattr; altura dupla se pdbl. Avança pcol.
draw_glyph:
        ld a,(prow)
        add a,a
        add a,a
        add a,a
        ld b,a
        ld de,GLYPHBUF
        ld a,(pdbl)
        or a
        jr nz,.dbl
        ld c,8
.l:     call .line
        inc de
        inc b
        dec c
        jr nz,.l
        jr .attr
.dbl:   ld c,8
.l2:    call .line
        inc b
        call .line
        inc b
        inc de
        dec c
        jr nz,.l2
.attr:  ld a,(prow)
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,(pcol)
        add a,l
        ld l,a
        ld a,h
        add a,ATTRS>>8
        ld h,a
        ld a,(pattr)
        ld (hl),a
        ld a,(pdbl)
        or a
        jr z,.adv
        ld bc,32
        add hl,bc
        ld a,(pattr)
        ld (hl),a
.adv:   ld hl,pcol
        inc (hl)
        ret
.line:  ld a,b
        call row_addr
        ld a,(pcol)
        add a,l
        ld l,a
        ld a,(de)
        ld (hl),a
        ret

; HL = 8 bytes de um ícone -> desenha em (prow,pcol)
put_icon:
        ld de,GLYPHBUF
        ld bc,8
        ldir
        jp draw_glyph

; HL = cadeia -> A = número de caracteres visíveis
strlen_u:
        ld b,0
.l:     ld a,(hl)
        inc hl
        or a
        jr z,.e
        cp 0xC3
        jr z,.l
        inc b
        jr .l
.e:     ld a,b
        ret

; HL = tabela de palavras, A = índice -> HL = palavra
table_ptr:
        add a,a
        ld e,a
        ld d,0
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        ret

; ---------------- som (beeper, bit 4 da porta 0xFE; borda preta) ----------------
; B = meio-período, C = meios-ciclos
tone:
        ld a,b
        ld (.p+1),a
        xor a
.l:     xor 0x10
        out (0xFE),a
.p:     ld b,0
.d:     djnz .d
        dec c
        jr nz,.l
        ret

; B = período inicial, C = meios-ciclos por passo, D = passos, E = variação do período
sweep:
.l:     push bc
        push de
        call tone
        pop de
        pop bc
        ld a,b
        add a,e
        ld b,a
        dec d
        jr nz,.l
        ret

snd_shoot:
        ld bc,0x1406
        ld de,0x0A06
        jr sweep
snd_pick:
        ld bc,0x5014
        ld de,0x06F4
        jr sweep
snd_door:
        ld bc,0xC00A
        ld de,0x0806
        jr sweep
snd_kill:
        ld bc,0x1E06
        ld de,0x0C08
        jr sweep
snd_tink:
        ld bc,0x100C
        ld de,0x0201
        jr sweep
snd_bump:
        ld bc,0xF006
        ld de,0x0200
        jr sweep
snd_stairs:
        ld bc,0x780A
        ld de,0x0AF8
        jr sweep
snd_die:
        ld bc,0x281E
        ld de,0x1808
        jr sweep
snd_hit:
        ld hl,0x0300                ; bytes da ROM como ruído
        ld bc,500
.l:     ld a,(hl)
        and 0x10
        out (0xFE),a
        inc hl
        ld e,5
.d:     dec e
        jr nz,.d
        dec bc
        ld a,b
        or c
        jr nz,.l
        ret
snd_victory:
        ld hl,victory_tune
.l:     ld b,(hl)
        inc hl
        ld c,(hl)
        inc hl
        ld a,b
        or a
        ret z
        push hl
        call tone
        pop hl
        jr .l
victory_tune:
        DB 120,60, 95,76, 80,90, 60,120, 80,90, 60,200, 0,0

; ---------------- números aleatórios (xorshift 16 bits) ----------------
rnd:
        ld hl,(seed)
        ld a,h
        rra
        ld a,l
        rra
        xor h
        ld h,a
        ld a,l
        rra
        ld a,h
        rra
        xor l
        ld l,a
        xor h
        ld h,a
        ld (seed),hl
        ret

; ---------------- teclado ----------------
; -> A = bits IN_*: Q A O P + ESPAÇO sempre; mais 6-0 (Sinclair) ou 5-8 + 0 (Cursor)
read_input:
        ld e,0
        ld bc,0xFBFE
        in a,(c)
        rra
        jr c,$+4
        set IN_U,e
        ld b,0xFD
        in a,(c)
        rra
        jr c,$+4
        set IN_D,e
        ld b,0xDF
        in a,(c)
        rra
        jr c,$+4
        set IN_R,e
        rra
        jr c,$+4
        set IN_L,e
        ld b,0x7F
        in a,(c)
        rra
        jr c,$+4
        set IN_F,e
        ld b,0xEF
        in a,(c)
        cpl
        ld d,a                      ; bit0 '0', bit1 '9', bit2 '8', bit3 '7', bit4 '6'
        bit 0,d
        jr z,$+4
        set IN_F,e
        ld a,(ctrl_mode)
        or a
        jr nz,.cur
        bit 1,d
        jr z,$+4
        set IN_U,e
        bit 2,d
        jr z,$+4
        set IN_D,e
        bit 3,d
        jr z,$+4
        set IN_R,e
        bit 4,d
        jr z,$+4
        set IN_L,e
        ld a,e
        ret
.cur:   bit 3,d
        jr z,$+4
        set IN_U,e
        bit 4,d
        jr z,$+4
        set IN_D,e
        bit 2,d
        jr z,$+4
        set IN_R,e
        ld b,0xF7
        in a,(c)
        bit 4,a
        jr nz,$+4
        set IN_L,e
        ld a,e
        ret

; NZ se alguma tecla estiver apertada
any_key:
        xor a
        in a,(0xFE)
        cpl
        and 0x1F
        ret

wait_release:
.l:     halt
        call any_key
        jr nz,.l
        ret

; =============================================================================
; Sprites (XOR, sem máscara: desenhar de novo apaga)
; =============================================================================
; HL = quadro (deslocamento 0), C = x, B = y, A = tamanho (0 = 16x16, 1 = 8x8)
draw_spr:
        ld d,a
        ld a,c
        and 6
        add a,a
        add a,a
        add a,a                     ; 8*(x&6) = 16 por deslocamento
        inc d
        dec d
        jr nz,.o8
        ld e,a
        add a,a
        add a,e                     ; 24*(x&6) = 48 por deslocamento
.o8:    add a,l
        ld l,a
        jr nc,.nc
        inc h
.nc:    ex de,hl                    ; DE = dados, H = tamanho
        ld a,h
        push af
        ld a,b
        call row_addr
        ld a,c
        rrca
        rrca
        rrca
        and 31
        add a,l
        ld l,a
        pop af
        or a
        jr nz,.s8
        ld b,16
        jr xor3
.s8:    ld b,8
        jr xor2

; DE = dados (3 bytes por linha), HL = tela, B = linhas
xor3:
.l:     ld a,(de)
        xor (hl)
        ld (hl),a
        inc de
        inc l
        ld a,(de)
        xor (hl)
        ld (hl),a
        inc de
        inc l
        ld a,(de)
        xor (hl)
        ld (hl),a
        inc de
        dec l
        dec l
        inc h
        ld a,h
        and 7
        jr nz,.ok
        ld a,l
        add a,32
        ld l,a
        jr c,.ok
        ld a,h
        sub 8
        ld h,a
.ok:    djnz .l
        ret

; DE = dados (2 bytes por linha), HL = tela, B = linhas
xor2:
.l:     ld a,(de)
        xor (hl)
        ld (hl),a
        inc de
        inc l
        ld a,(de)
        xor (hl)
        ld (hl),a
        inc de
        dec l
        inc h
        ld a,h
        and 7
        jr nz,.ok
        ld a,l
        add a,32
        ld l,a
        jr c,.ok
        ld a,h
        sub 8
        ld h,a
.ok:    djnz .l
        ret

; IX = objeto: apaga o desenho antigo e desenha o novo (só se mudou)
render_obj:
        ld a,(ix+O_OVIS)
        or a
        jr z,.new
        ld a,(ix+O_VIS)
        or a
        jr z,.erase
        ld a,(ix+O_X)
        cp (ix+O_OX)
        jr nz,.erase
        ld a,(ix+O_Y)
        cp (ix+O_OY)
        jr nz,.erase
        ld a,(ix+O_GFX)
        cp (ix+O_OGFX)
        jr nz,.erase
        ld a,(ix+O_GFX+1)
        cp (ix+O_OGFX+1)
        jr nz,.erase
        ld a,(ix+O_SIZE)
        cp (ix+O_OSIZE)
        ret z
.erase: ld c,(ix+O_OX)
        ld b,(ix+O_OY)
        ld l,(ix+O_OGFX)
        ld h,(ix+O_OGFX+1)
        ld a,(ix+O_OSIZE)
        call draw_spr
.new:   ld a,(ix+O_VIS)
        ld (ix+O_OVIS),a
        or a
        ret z
        ld c,(ix+O_X)
        ld (ix+O_OX),c
        ld b,(ix+O_Y)
        ld (ix+O_OY),b
        ld l,(ix+O_GFX)
        ld (ix+O_OGFX),l
        ld h,(ix+O_GFX+1)
        ld (ix+O_OGFX+1),h
        ld a,(ix+O_SIZE)
        ld (ix+O_OSIZE),a
        jp draw_spr

; Desenha todos os objetos e acerta as cores (atributos) por baixo deles
render_all:
        ld ix,objects
        ld b,NUM_OBJ
.p:     push bc
        call render_obj
        pop bc
        ld de,OBJ_SIZE
        add ix,de
        djnz .p
        ld ix,objects
        ld b,NUM_OBJ
.r:     push bc
        ld a,(ix+O_AVIS)
        or a
        jr z,.rn
        ld c,(ix+O_AX)
        ld b,(ix+O_AY)
        ld a,(ix+O_ASIZE)
        ld d,0xFF
        call attr_box
.rn:    pop bc
        ld de,OBJ_SIZE
        add ix,de
        djnz .r
        ld ix,objects+OBJ_SIZE*(NUM_OBJ-1)   ; pinta do último ao primeiro:
        ld b,NUM_OBJ                          ; a cor do jogador prevalece
.q:     push bc
        ld a,(ix+O_VIS)
        ld (ix+O_AVIS),a
        or a
        jr z,.qn
        ld c,(ix+O_X)
        ld (ix+O_AX),c
        ld b,(ix+O_Y)
        ld (ix+O_AY),b
        ld a,(ix+O_SIZE)
        ld (ix+O_ASIZE),a
        ld d,(ix+O_INK)
        call attr_box
.qn:    pop bc
        ld de,-OBJ_SIZE
        add ix,de
        djnz .q
        ret

; C = x, B = y, A = tamanho, D = tinta (0xFF = restaurar o fundo)
; Pinta só as células de chão (CMAP = 0), mantendo papel e brilho do fundo.
attr_box:
        ld e,15
        or a
        jr z,.s
        ld e,7
.s:     ld a,c
        add a,e
        rrca
        rrca
        rrca
        and 31
        ld (ab_c1),a
        ld a,b
        add a,e
        rrca
        rrca
        rrca
        and 31
        ld (ab_r1),a
        ld a,c
        rrca
        rrca
        rrca
        and 31
        ld c,a
        ld a,b
        rrca
        rrca
        rrca
        and 31
        ld b,a
        ld a,(ab_c1)
        sub c
        inc a
        ld (ab_nc),a
        ld a,(ab_r1)
        sub b
        inc a
        ld e,a
        ld l,b
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,l
        or c
        ld l,a
        ld a,h
        add a,ATTRS>>8
        ld h,a
.row:   push hl
        ld a,(ab_nc)
        ld b,a
.cell:  ld a,h
        add a,(CMAP-ATTRS)>>8
        ld h,a
        ld c,(hl)
        inc h
        inc h
        inc h                       ; BGATTR = CMAP + 0x300
        ld a,d
        inc a
        jr nz,.paint
        ld a,(hl)
        jr .put
.paint: ld a,c
        or a
        jr nz,.skip
        ld a,(hl)
        and 0x78                    ; papel e brilho do fundo
        or d
.put:   ld c,a
        ld a,h
        sub (BGATTR-ATTRS)>>8
        ld h,a
        ld (hl),c
        jr .nx
.skip:  ld a,h
        sub (BGATTR-ATTRS)>>8
        ld h,a
.nx:    inc hl
        djnz .cell
        pop hl
        ld bc,32
        add hl,bc
        dec e
        jr nz,.row
        ret

; =============================================================================
; Salas
; =============================================================================
; A = sala -> HL = registro
room_addr:
        ld l,a
        ld h,0
        ld d,h
        ld e,l
        add hl,hl
        add hl,hl
        add hl,de
        add hl,hl
        add hl,hl
        add hl,de
        ld de,rooms
        add hl,de
        ret

; B = linha, C = coluna -> HL = CMAP + linha*32 + coluna (preserva BC, DE)
cmap_addr:
        ld l,b
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld a,l
        or c
        ld l,a
        ld a,h
        add a,CMAP>>8
        ld h,a
        ret

; C = x, B = y, D = largura, E = altura, A = ladrilho (em células)
fill_rect:
        ld (fr_tile),a
.row:   push bc
        push de
        call cmap_addr
        ld a,(fr_tile)
        ld b,d
.c:     ld (hl),a
        inc hl
        djnz .c
        pop de
        pop bc
        inc b
        dec e
        jr nz,.row
        ret

; A = sala: monta e desenha a sala, reinicia inimigos
enter_room:
        ld (cur_room),a
        call room_addr
        ld (room_ptr),hl
        call setup_tiles
        call build_cmap
        call render_room
        call hud_room
        ld ix,objects
        ld b,NUM_OBJ
.o:     ld (ix+O_OVIS),0
        ld (ix+O_AVIS),0
        ld de,OBJ_SIZE
        add ix,de
        djnz .o
        xor a
        ld (obj_proj+O_STATE),a
        ld (obj_proj+O_VIS),a
        ld a,(obj_player+O_X)
        ld (entry_x),a
        ld c,a
        ld a,(obj_player+O_Y)
        ld (entry_y),a
        ld b,a
        call box_check16
        ld a,(box_stairs)
        ld (on_stair),a
        jp setup_enemies

; Tabelas de desenho/cor dos ladrilhos para a sala atual
setup_tiles:
        ld ix,(room_ptr)
        ld hl,blank_tile
        ld (tile_ptr),hl
        ld a,(ix+R_FATTR)
        ld (tile_attr),a
        ld (cur_fattr),a
        ld a,(ix+R_STYLE)
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,GFX_WALLS
        add hl,de
        ld (tile_ptr+2),hl
        ld a,(ix+R_WATTR)
        ld (tile_attr+1),a
        ld hl,GFX_DOOR
        ld (tile_ptr+4),hl
        ld (tile_ptr+6),hl
        ld (tile_ptr+8),hl
        ld (tile_ptr+10),hl
        ld hl,door_attr
        ld de,tile_attr+2
        ld bc,4
        ldir
        ld hl,GFX_MASTER
        ld (tile_ptr+12),hl
        ld a,MASTER_ATTR
        ld (tile_attr+6),a
        ld hl,GFX_UP
        ld (tile_ptr+14),hl
        ld hl,GFX_DOWN
        ld (tile_ptr+16),hl
        ld a,(ix+R_FATTR)
        and 0x38
        or 0x47
        ld (tile_attr+7),a
        ld (tile_attr+8),a
        ld hl,GFX_DECO1
        ld (tile_ptr+18),hl
        ld hl,GFX_DECO2
        ld (tile_ptr+20),hl
        ld a,(ix+R_DATTR)
        ld (tile_attr+9),a
        ld (tile_attr+10),a
        ret

; Monta o CMAP: paredes, vãos de porta (abertos ou trancados), mobília, escada
build_cmap:
        ld hl,CMAP
        ld de,CMAP+1
        ld bc,639
        ld (hl),T_WALL
        ldir
        ld c,2
        ld b,2
        ld d,28
        ld e,16
        xor a
        call fill_rect
        ld hl,(room_ptr)
        ld de,R_EXITS
        add hl,de
        ld b,0
.side:  push bc
        push hl
        ld a,(hl)
        cp NO_EXIT
        jr z,.nx
        inc hl
        ld a,(hl)
        call gap_tile
        ld (fr_tile2),a
        pop hl
        pop bc
        push bc
        push hl
        ld a,b
        call gap_rect
        ld a,(fr_tile2)
        call fill_rect
.nx:    pop hl
        pop bc
        inc hl
        inc hl
        inc b
        ld a,b
        cp 4
        jr nz,.side
        ld ix,(room_ptr)
        ld a,(ix+R_TMPL)
        ld hl,templates
        call table_ptr
        ld b,(hl)
        inc hl
        ld a,b
        or a
        jr z,.st
.t:     push bc
        ld c,(hl)
        inc hl
        ld b,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld e,(hl)
        inc hl
        ld a,(hl)
        inc hl
        push hl
        call fill_rect
        pop hl
        pop bc
        djnz .t
.st:    ld ix,(room_ptr)
        ld a,(ix+R_SDEST)
        cp NO_EXIT
        ret z
        ld c,(ix+R_SX)
        ld b,(ix+R_SY)
        ld d,2
        ld e,2
        ld a,(ix+R_STYPE)
        jp fill_rect

; A = lado (0 N, 1 S, 2 O, 3 L) -> C,B,D,E = retângulo do vão
gap_rect:
        add a,a
        add a,a
        ld e,a
        ld d,0
        ld hl,gaps
        add hl,de
        ld c,(hl)
        inc hl
        ld b,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld e,(hl)
        ret
gaps:   DB 14,0,4,2, 14,18,4,2, 0,8,2,4, 30,8,2,4

; A = link -> A = ladrilho do vão (0 se livre ou já aberto)
gap_tile:
        ld e,a
        ld d,0
        ld hl,link_open
        add hl,de
        ld a,(hl)
        or a
        jr nz,.open
        ld hl,links_lock
        add hl,de
        ld a,(hl)
        ret
.open:  xor a
        ret

; B = linha, C = coluna: desenha a célula do CMAP e grava o BGATTR
draw_cell:
        call cmap_addr
        ld e,(hl)
        inc h
        inc h
        inc h
        push hl
        ld d,0
        ld hl,tile_attr
        add hl,de
        ld a,(hl)
        pop hl
        ld (hl),a
        ld hl,tile_ptr
        add hl,de
        add hl,de
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        ld a,b
        and 1
        add a,a
        ld d,a
        ld a,c
        and 1
        or d
        add a,a
        add a,a
        add a,a
        ld e,a
        ld d,0
        add hl,de
        ex de,hl
        ld a,b
        and 0x18
        or 0x40
        ld h,a
        ld a,b
        and 7
        rrca
        rrca
        rrca
        or c
        ld l,a
        ld b,8
.l:     ld a,(de)
        ld (hl),a
        inc de
        inc h
        djnz .l
        ret

; C = x, B = y, D = largura, E = altura: redesenha células e copia as cores
redraw_rect:
.r:     push bc
        push de
.c:     push bc
        push de
        call draw_cell
        pop de
        pop bc
        inc c
        dec d
        jr nz,.c
        pop de
        pop bc
        inc b
        dec e
        jr nz,.r
copy_bgattr:
        ld hl,BGATTR
        ld de,ATTRS
        ld bc,640
        ldir
        ret

render_room:
        ld hl,ATTRS                 ; esconde a área durante o desenho
        ld de,ATTRS+1
        ld bc,639
        ld (hl),0
        ldir
        ld b,0
.r:     ld c,0
.c:     push bc
        call draw_cell
        pop bc
        inc c
        ld a,c
        cp 32
        jr nz,.c
        inc b
        ld a,b
        cp 20
        jr nz,.r
        call draw_room_items
        jr copy_bgattr

draw_room_items:
        ld hl,items_def
        ld de,item_taken
        ld b,NUM_ITEMS
.l:     ld a,(cur_room)
        cp (hl)
        jr nz,.n
        ld a,(de)
        or a
        jr nz,.n
        push bc
        push de
        push hl
        xor a
        call draw_item
        pop hl
        pop de
        pop bc
.n:     inc hl
        inc hl
        inc hl
        inc hl
        inc de
        djnz .l
        ret

; A = tipo de objeto -> HL = item_kinds + A*8
kind_addr:
        add a,a
        add a,a
        add a,a
        ld l,a
        ld h,0
        ld de,item_kinds
        add hl,de
        ret

; HL = registro do objeto, A = 0 desenha / 1 apaga (XOR + cor do chão)
draw_item:
        ld (di_mode),a
        inc hl
        ld c,(hl)
        inc hl
        ld b,(hl)
        inc hl
        ld a,(hl)
        push bc
        call kind_addr
        inc hl
        inc hl
        ld a,(hl)
        ld (di_attr),a
        inc hl
        inc hl
        ld e,(hl)
        inc hl
        ld d,(hl)
        pop bc
        push bc
        ld a,b
        add a,a
        add a,a
        add a,a
        call row_addr
        ld a,l
        add a,c
        ld l,a
        ld b,16
        call xor2
        pop bc
        call cmap_addr
        inc h
        inc h
        inc h
        call .pair
        ld de,31
        add hl,de
.pair:  call .cell
        inc hl
.cell:  ld a,(di_mode)
        or a
        jr nz,.er
        ld a,(hl)
        and 0x78
        ld e,a
        ld a,(di_attr)
        or e
        jr .st
.er:    ld a,(cur_fattr)
.st:    ld (hl),a
        ld e,a
        ld a,h
        sub (BGATTR-ATTRS)>>8
        ld h,a
        ld (hl),e
        ld a,h
        add a,(BGATTR-ATTRS)>>8
        ld h,a
        ret

; =============================================================================
; Painel (linhas 20 a 23)
; =============================================================================
hud_full:
        ld b,20
        ld c,4
        ld a,HUD_ATTR
        call clear_rows
        xor a
        ld (pdbl),a
        ld hl,hud_labels
        call print
        call hud_room
        call hud_energy
        call hud_lives
        call hud_score
        jp hud_inv

hud_room:
        ld b,20
        ld c,1
        ld a,HUD_NAME_ATTR
        call clear_rows
        xor a
        ld (pdbl),a
        ld ix,(room_ptr)
        ld a,(ix+R_NAME)
        ld hl,room_names
        call table_ptr
        push hl
        call strlen_u
        pop hl
        ld b,a
        ld a,32
        sub b
        srl a
        ld (pcol),a
        ld a,20
        ld (prow),a
        ld a,HUD_NAME_ATTR
        ld (pattr),a
        call print
        ld a,23
        ld (prow),a
        ld a,14
        ld (pcol),a
        ld a,HUD_ATTR
        ld (pattr),a
        ld b,18
.sp:    push bc
        ld a,' '
        call putc
        pop bc
        djnz .sp
        ld ix,(room_ptr)
        ld a,(ix+R_FLOOR)
        ld hl,floor_names
        call table_ptr
        push hl
        call strlen_u
        pop hl
        ld b,a
        ld a,32
        sub b
        ld (pcol),a
        ld a,HUD_FLOOR_ATTR
        ld (pattr),a
        jp print

hud_energy:
        ld a,(energy)
        ld (hud_en_drawn),a
        ld c,a
        ld de,ENBUF
        ld b,16
.l:     ld a,c
        cp 8
        jr c,.part
        sub 8
        ld c,a
        ld a,0xFF
        jr .st
.part:  ld hl,bar_mask
        add a,l
        ld l,a
        jr nc,.nc
        inc h
.nc:    ld a,(hl)
        ld c,0
.st:    ld (de),a
        inc de
        djnz .l
        ld b,6
        ld a,21*8+1
.ln:    push af
        push bc
        call row_addr
        ld de,8
        add hl,de
        ex de,hl
        ld hl,ENBUF
        ld bc,16
        ldir
        pop bc
        pop af
        inc a
        djnz .ln
        ld a,(energy)
        cp ENERGY_MAX/4
        ld a,BAR_ATTR_OK
        jr nc,.at
        ld a,BAR_ATTR_LOW
.at:    ld hl,ATTRS+21*32+8
        ld b,16
.al:    ld (hl),a
        inc hl
        djnz .al
        ret
bar_mask:
        DB 0x00,0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE

hud_lives:
        ld a,21
        ld (prow),a
        ld a,31
        ld (pcol),a
        ld a,HUD_ATTR
        ld (pattr),a
        xor a
        ld (pdbl),a
        ld a,(lives)
        add a,'0'
        jp putc

hud_score:
        ld a,22
        ld (prow),a
        ld a,7
        ld (pcol),a
        ld a,HUD_ATTR
        ld (pattr),a
print_score:
        ld hl,score
        ld b,3
.l:     ld a,(hl)
        rrca
        rrca
        rrca
        rrca
        and 15
        add a,'0'
        push hl
        push bc
        call putc
        pop bc
        pop hl
        ld a,(hl)
        and 15
        add a,'0'
        push hl
        push bc
        call putc
        pop bc
        pop hl
        inc hl
        djnz .l
        ret

hud_inv:
        ld a,22
        ld (prow),a
        ld c,0
.k:     ld a,c
        add a,a
        add a,22
        ld (pcol),a
        ld e,c
        ld d,0
        ld hl,key_icon_attr
        add hl,de
        ld a,(hl)
        ld (pattr),a
        ld a,(keys)
        ld b,c
        inc b
.sh:    rrca
        djnz .sh
        push bc
        ld hl,ICON_KEY
        jr c,.has
        ld hl,blank_tile
.has:   call put_icon
        pop bc
        inc c
        ld a,c
        cp 4
        jr nz,.k
        ld a,23
        ld (prow),a
        ld c,0
.p:     ld a,c
        add a,a
        add a,7
        ld (pcol),a
        ld a,(pieces)
        ld b,a
        ld a,c
        cp b
        ld hl,ICON_PIECE
        ld a,PIECE_ATTR
        jr c,.ph
        ld hl,ICON_EMPTY
        ld a,EMPTY_ATTR
.ph:    ld (pattr),a
        push bc
        call put_icon
        pop bc
        inc c
        ld a,c
        cp PIECES_NEEDED
        jr nz,.p
        ret

hud_update:
        ld a,(energy)
        ld hl,hud_en_drawn
        cp (hl)
        call nz,hud_energy
        ld a,(hud_dirty)
        or a
        ret z
        xor a
        ld (hud_dirty),a
        call hud_score
        call hud_inv
        jp hud_lives

; =============================================================================
; Telas de título e de fim
; =============================================================================
title_screen:
        call wait_release
        ld a,TITLE_ATTR
        call cls
        xor a
        ld (pdbl),a
        ld (title_page),a
        ld hl,game_title
        call print
.page:  xor a
        ld (pdbl),a
        ld b,TITLE_PAGE_ROW
        ld c,TITLE_PAGE_ROWS
        ld a,TITLE_ATTR
        call clear_rows
        ld a,(title_page)
        or a
        ld hl,game_page1
        jr z,.p1
        ld hl,game_page2
.p1:    call print
        ld a,(title_page)
        or a
        call nz,game_page2_hook
        call title_mode
        ld bc,TITLE_PAGE_FRAMES
.wait:  halt
        push bc
        ld a,0xF7
        in a,(0xFE)
        bit 0,a
        jr nz,.k2
        xor a
        ld (ctrl_mode),a
        call title_mode
        jr .k0
.k2:    bit 1,a
        jr nz,.k0
        ld a,1
        ld (ctrl_mode),a
        call title_mode
.k0:    ld a,0x7F
        in a,(0xFE)
        rra
        jr nc,.start
        ld a,0xEF
        in a,(0xFE)
        rra
        jr nc,.start
        ld a,0xBF
        in a,(0xFE)
        rra
        jr nc,.start
        pop bc
        dec bc
        ld a,b
        or c
        jr nz,.wait
        ld a,(title_page)
        xor 1
        ld (title_page),a
        jr .page
.start: pop bc
        jp snd_pick

title_mode:
        xor a
        ld (pdbl),a
        ld a,(ctrl_mode)
        or a
        ld hl,txt_mode0
        jr z,.m
        ld hl,txt_mode1
.m:     jp print

victory_screen:
        call snd_victory
        ld hl,txt_victory
        jr end_screen
gameover_screen:
        ld hl,txt_gameover
end_screen:
        push hl
        ld a,TITLE_ATTR
        call cls
        xor a
        ld (pdbl),a
        pop hl
        call print
        ld a,END_SCORE_ROW
        ld (prow),a
        ld a,END_SCORE_COL
        ld (pcol),a
        call print_score
        ld b,75
.w:     halt
        djnz .w
        call wait_release
.k:     halt
        call any_key
        jr z,.k
        ret

; =============================================================================
; Partida
; =============================================================================
game_run:
        ld a,ENERGY_MAX
        ld (energy),a
        ld a,LIVES
        ld (lives),a
        xor a
        ld (keys),a
        ld (pieces),a
        ld (game_result),a
        ld (score),a
        ld (score+1),a
        ld (score+2),a
        ld (fire_cd),a
        ld (bump_cd),a
        ld (flash_cnt),a
        ld (walk_cnt),a
        ld (hud_dirty),a
        ld hl,state_area
        ld de,state_area+1
        ld bc,STATE_LEN-1
        ld (hl),0
        ldir
        ld a,(FRAMES)
        ld l,a
        ld a,r
        ld h,a
        set 0,l
        ld (seed),hl
        ld ix,obj_player
        ld (ix+O_VIS),1
        ld (ix+O_SIZE),0
        ld (ix+O_INK),PLAYER_INK
        ld (ix+O_X),START_X
        ld (ix+O_Y),START_Y
        ld a,1
        ld (facing),a
        call set_player_gfx
        ld a,DRAIN_TICKS
        ld (drain_cnt),a
        ld a,HUD_ATTR
        call cls
        ld a,START_ROOM
        call enter_room
        call hud_full
        ld a,(FRAMES)
        ld (last_frame),a
.loop:  halt
        ld a,(FRAMES)
        ld hl,last_frame
        sub (hl)
        cp 2
        jr c,.loop
        ld a,(FRAMES)
        ld (last_frame),a
        call render_all
        call hud_update
        ld hl,tick
        inc (hl)
        ld a,(flash_cnt)
        or a
        jr z,.nf
        dec a
        ld (flash_cnt),a
        jr nz,.nf
        out (0xFE),a
.nf:    ld a,(bump_cd)
        or a
        jr z,.nb
        dec a
        ld (bump_cd),a
.nb:    call read_input
        ld (input),a
        call player_update
        ld a,(game_result)
        or a
        jr nz,.end
        call proj_update
        call enemies_update
        call collide
        call items_check
        call energy_tick
        ld a,(energy)
        or a
        jr nz,.loop
        call player_die
        ld a,(game_result)
        or a
        jr z,.loop
.end:   cp 1
        jp z,victory_screen
        jp gameover_screen

set_player_gfx:
        ld a,(facing)
        add a,a
        ld b,a
        ld a,(walk_cnt)
        rrca
        rrca
        and 1
        add a,b
        add a,PLAYER_SPR
        call spr16_ptr
        ld (obj_player+O_GFX),hl
        ret

; ---------------- jogador ----------------
player_update:
        ld ix,obj_player
        xor a
        ld (moved),a
        ld a,(input)
        and 3
        jr z,.v
        bit IN_L,a
        jr z,.right
        ld a,2
        ld (facing),a
        ld a,(ix+O_X)
        sub 2
        ld c,a
        ld b,(ix+O_Y)
        call pl_try
        jr .v
.right: ld a,3
        ld (facing),a
        ld a,(ix+O_X)
        add a,2
        ld c,a
        ld b,(ix+O_Y)
        call pl_try
.v:     ld a,(input)
        and 0x0C
        jr z,.done
        ld b,a
        ld a,(input)
        and 3
        jr nz,.nf
        ld a,b
        and 0x08
        ld a,0
        jr nz,.f
        inc a
.f:     ld (facing),a
.nf:    ld a,(input)
        bit IN_U,a
        jr z,.down
        ld a,(ix+O_Y)
        sub 2
        ld b,a
        ld c,(ix+O_X)
        call pl_try
        jr .done
.down:  ld a,(ix+O_Y)
        add a,2
        ld b,a
        ld c,(ix+O_X)
        call pl_try
.done:  ld a,(moved)
        or a
        jr z,.an
        ld hl,walk_cnt
        inc (hl)
.an:    call set_player_gfx
        ld c,(ix+O_X)
        ld b,(ix+O_Y)
        call box_check16
        ld a,(box_stairs)
        or a
        jr nz,.onst
        ld (on_stair),a
        jr .exits
.onst:  ld a,(on_stair)
        or a
        jp z,take_stairs
.exits: ld a,(ix+O_Y)
        cp 4
        jr c,.goN
        cp 140
        jr nc,.goS
        ld a,(ix+O_X)
        cp 4
        jr c,.goW
        cp 236
        jr nc,.goE
        ret
.goN:   xor a
        jr .go
.goS:   ld a,1
        jr .go
.goW:   ld a,2
        jr .go
.goE:   ld a,3
.go:    ld (go_side),a
        add a,a
        ld e,a
        ld d,0
        ld hl,(room_ptr)
        add hl,de
        ld de,R_EXITS
        add hl,de
        ld a,(hl)
        cp EXIT_OUT
        jr z,.win
        cp NO_EXIT
        ret z
        push af
        ld a,(go_side)
        or a
        jr nz,.g1
        ld (ix+O_Y),128
        jr .gg
.g1:    dec a
        jr nz,.g2
        ld (ix+O_Y),16
        jr .gg
.g2:    dec a
        jr nz,.g3
        ld (ix+O_X),224
        jr .gg
.g3:    ld (ix+O_X),16
.gg:    pop af
        jp enter_room
.win:   ld a,1
        ld (game_result),a
        ld de,WIN_BONUS
        jp add_score

; C = x, B = y: move o jogador se couber; porta trancada -> tenta abrir
pl_try:
        ld a,c
        cp 241
        ret nc
        ld a,b
        cp 145
        ret nc
        push bc
        call box_check16
        pop bc
        or a
        jr nz,.blk
        ld (ix+O_X),c
        ld (ix+O_Y),b
        ld a,1
        ld (moved),a
        ret
.blk:   cp T_DOOR
        ret c
        cp T_MASTER+1
        ret nc
        ; porta: A = 2..6
        cp T_MASTER
        jr z,.m
        sub T_DOOR
        ld b,a
        inc b
        ld a,0x80
.sh:    rlca
        djnz .sh
        ld hl,keys
        and (hl)
        jr z,.no
        jr .open
.m:     ld a,(pieces)
        cp PIECES_NEEDED
        jr c,.no
.open:  ld a,(ix+O_Y)
        cp 32
        ld a,0
        jr c,open_side
        ld a,(ix+O_Y)
        cp 112
        ld a,1
        jr nc,open_side
        ld a,(ix+O_X)
        cp 32
        ld a,2
        jr c,open_side
        ld a,3
        jr open_side
.no:    ld a,(bump_cd)
        or a
        ret nz
        ld a,15
        ld (bump_cd),a
        jp snd_bump

; A = lado: abre a porta (para sempre) e redesenha o vão
open_side:
        ld (go_side),a
        add a,a
        ld e,a
        ld d,0
        ld hl,(room_ptr)
        add hl,de
        ld de,R_EXITS+1
        add hl,de
        ld e,(hl)
        ld d,0
        ld hl,link_open
        add hl,de
        ld (hl),1
        ld a,(go_side)
        call gap_rect
        push bc
        push de
        xor a
        call fill_rect
        pop de
        pop bc
        call redraw_rect
        ld de,0x0020
        call add_score
        jp snd_door

take_stairs:
        ld hl,(room_ptr)
        ld de,R_SDEST
        add hl,de
        ld a,(hl)
        push af
        call room_addr
        ld de,R_SX
        add hl,de
        ld a,(hl)
        add a,a
        add a,a
        add a,a
        ld (obj_player+O_X),a
        inc hl
        ld a,(hl)
        add a,a
        add a,a
        add a,a
        ld (obj_player+O_Y),a
        call snd_stairs
        pop af
        jp enter_room

; C = x, B = y (caixa 16x16 ou 8x8) -> A = ladrilho sólido tocado (0 = livre;
; porta tem prioridade sobre parede, para abrir mesmo encostando de viés)
; box_stairs = escada tocada (0 = nenhuma). Não preserva BC.
box_check16:
        ld e,15
        jr box_check
box_check8:
        ld e,7
box_check:
        xor a
        ld (box_stairs),a
        ld (box_solid),a
        ld a,c
        add a,e
        rrca
        rrca
        rrca
        and 31
        ld (ab_c1),a
        ld a,b
        add a,e
        rrca
        rrca
        rrca
        and 31
        ld d,a
        ld a,c
        rrca
        rrca
        rrca
        and 31
        ld c,a
        ld a,b
        rrca
        rrca
        rrca
        and 31
        ld b,a
        ld a,d
        sub b
        inc a
        ld e,a
        ld a,(ab_c1)
        sub c
        inc a
        ld d,a
        call cmap_addr
.row:   push hl
        ld b,d
.cell:  ld a,(hl)
        or a
        jr z,.nx
        cp T_UP
        jr z,.st
        cp T_DOWN
        jr z,.st
        cp T_MASTER+1
        jr nc,.sol
        cp T_DOOR
        jr nc,.door
.sol:   ld c,a                      ; parede/mobília: só se ainda não há porta
        ld a,(box_solid)
        or a
        jr nz,.nx
        ld a,c
.door:  ld (box_solid),a
        jr .nx
.st:    ld (box_stairs),a
.nx:    inc hl
        djnz .cell
        pop hl
        ld bc,32
        add hl,bc
        dec e
        jr nz,.row
        ld a,(box_solid)
        ret

; ---------------- projétil ----------------
proj_update:
        ld ix,obj_proj
        ld a,(fire_cd)
        or a
        jr z,.fc
        dec a
        ld (fire_cd),a
.fc:    ld a,(ix+O_STATE)
        or a
        jr nz,.move
        ld a,(input)
        bit IN_F,a
        ret z
        ld a,(fire_cd)
        or a
        ret nz
        ld a,(obj_player+O_X)
        add a,4
        ld (ix+O_X),a
        ld a,(obj_player+O_Y)
        add a,4
        ld (ix+O_Y),a
        ld a,(facing)
        add a,a
        ld e,a
        ld d,0
        ld hl,dir_vec
        add hl,de
        ld a,(hl)
        ld (ix+O_DX),a
        inc hl
        ld a,(hl)
        ld (ix+O_DY),a
        ld (ix+O_STATE),1
        ld (ix+O_TIMER),PROJ_RANGE
        ld (ix+O_VIS),1
        ld (ix+O_SIZE),1
        ld (ix+O_INK),PROJ_INK
        call snd_shoot
        jr .gfx
.move:  dec (ix+O_TIMER)
        jr z,proj_kill
        ld a,(ix+O_X)
        add a,(ix+O_DX)
        ld c,a
        cp 249
        jr nc,proj_kill
        ld a,(ix+O_Y)
        add a,(ix+O_DY)
        ld b,a
        cp 153
        jr nc,proj_kill
        push bc
        call box_check8
        pop bc
        or a
        jr nz,proj_kill
        ld (ix+O_X),c
        ld (ix+O_Y),b
.gfx:   ld a,(tick)
        and 1
        add a,PROJ_SPR8
        call spr8_ptr
        ld (ix+O_GFX),l
        ld (ix+O_GFX+1),h
        ret
proj_kill:
        ld (ix+O_STATE),0
        ld (ix+O_VIS),0
        ld a,6
        ld (fire_cd),a
        ret
dir_vec:
        DB 0,0xFC, 0,4, 0xFC,0, 4,0   ; cima, baixo, esquerda, direita

; ---------------- inimigos ----------------
setup_enemies:
        ld ix,(room_ptr)
        ld a,(ix+R_ETYPE)
        ld (en_type),a
        ld a,(ix+R_ECOUNT)
        ld (en_count),a
        ld ix,obj_en0
        ld b,0
.l:     ld (ix+O_VIS),0
        ld (ix+O_STATE),ES_OFF
        ld a,(en_count)
        ld c,a
        ld a,b
        cp c
        jr nc,.n
        ld (ix+O_STATE),ES_WAIT
        ld a,b
        add a,a
        add a,a
        ld c,a
        add a,a
        add a,a
        add a,c
        add a,10
        ld (ix+O_TIMER),a
        ld a,(en_type)
        bit 7,a
        jr z,.t
        and 0x7F
        add a,b
.m:     cp NUM_ETYPES
        jr c,.t
        sub NUM_ETYPES
        jr .m
.t:     ld (ix+O_TYPE),a
.n:     ld de,OBJ_SIZE
        add ix,de
        inc b
        ld a,b
        cp MAX_EN
        jr nz,.l
        ret

; A = tipo -> HL = enemy_types + A*8
etype_addr:
        add a,a
        add a,a
        add a,a
        ld l,a
        ld h,0
        ld de,enemy_types
        add hl,de
        ret

enemies_update:
        ld ix,obj_en0
        ld b,MAX_EN
.l:     push bc
        call enemy_one
        pop bc
        ld de,OBJ_SIZE
        add ix,de
        djnz .l
        ret

enemy_one:
        ld a,(ix+O_STATE)
        or a
        ret z
        cp ES_WAIT
        jr z,.wait
        cp ES_APPEAR
        jr z,.appear
        cp ES_ALIVE
        jp z,en_alive
        dec (ix+O_TIMER)            ; morrendo
        jp nz,en_poof_gfx
        ld (ix+O_VIS),0
        ld (ix+O_STATE),ES_WAIT
        call rnd
        and 63
        add a,RESPAWN_TICKS
        ld (ix+O_TIMER),a
        ret
.wait:  dec (ix+O_TIMER)
        ret nz
        call find_spawn
        jr c,.ok
        ld (ix+O_TIMER),10
        ret
.ok:    ld (ix+O_STATE),ES_APPEAR
        ld (ix+O_TIMER),10
        ld (ix+O_VIS),1
        ld (ix+O_SIZE),0
        ld (ix+O_INK),POOF_INK
        jp en_poof_gfx
.appear:
        dec (ix+O_TIMER)
        jp nz,en_poof_gfx
        ld (ix+O_STATE),ES_ALIVE
        ld a,(ix+O_TYPE)
        call etype_addr
        inc hl
        ld a,(hl)
        ld (ix+O_INK),a
        inc hl
        inc hl
        inc hl
        ld a,(hl)
        ld (ix+O_HP),a
        ld (ix+O_DX),2
        ld (ix+O_DY),2
        ld (ix+O_DTIM),0
        jp en_alive

en_poof_gfx:
        ld a,(ix+O_TIMER)
        rrca
        and 1
        add a,POOF_SPR
        call spr16_ptr
        ld (ix+O_GFX),l
        ld (ix+O_GFX+1),h
        ret

en_alive:
        ld a,(ix+O_TYPE)
        call etype_addr
        push hl
        ld a,(tick)
        rrca
        rrca
        and 1
        add a,(hl)
        call spr16_ptr
        ld (ix+O_GFX),l
        ld (ix+O_GFX+1),h
        pop hl
        inc hl
        inc hl
        ld a,(hl)
        ld (beh),a
        inc hl
        ld a,(tick)
        and (hl)
        ret nz
        ld a,(beh)
        or a
        jp z,beh_ghost
        dec a
        jp z,beh_bounce
        jp beh_chase

; A = alvo, C = atual -> A = passo (0, 2 ou -2), E = distância
towards:
        sub c
        jr z,.z
        jr c,.n
        ld e,a
        ld a,2
        ret
.n:     neg
        ld e,a
        ld a,0xFE
        ret
.z:     ld e,0
        ret

; C = x, B = y: move o inimigo se couber no chão -> carry = moveu
en_try:
        ld a,c
        cp 16
        jr c,.no
        cp 225
        jr nc,.no
        ld a,b
        cp 16
        jr c,.no
        cp 129
        jr nc,.no
        push bc
        call box_check16
        pop bc
        or a
        jr nz,.no
        ld (ix+O_X),c
        ld (ix+O_Y),b
        scf
        ret
.no:    or a
        ret

; Comportamento 0: persegue em diagonal e atravessa a mobília (fantasma)
beh_ghost:
        ld c,(ix+O_X)
        ld a,(obj_player+O_X)
        call towards
        add a,c
        cp 16
        jr c,.y
        cp 225
        jr nc,.y
        ld (ix+O_X),a
.y:     ld c,(ix+O_Y)
        ld a,(obj_player+O_Y)
        call towards
        add a,c
        cp 16
        ret c
        cp 129
        ret nc
        ld (ix+O_Y),a
        ret

; Comportamento 1: voo em diagonal, quica nas paredes, mira o jogador às vezes
beh_bounce:
        ld a,(ix+O_DTIM)
        or a
        jr z,.new
        dec (ix+O_DTIM)
        jr .mv
.new:   call rnd
        and 15
        add a,12
        ld (ix+O_DTIM),a
        ld c,(ix+O_X)
        ld a,(obj_player+O_X)
        call towards
        or a
        jr nz,.x1
        ld a,2
.x1:    ld (ix+O_DX),a
        ld c,(ix+O_Y)
        ld a,(obj_player+O_Y)
        call towards
        or a
        jr nz,.y1
        ld a,2
.y1:    ld (ix+O_DY),a
        call rnd
        and 3
        jr nz,.mv
        ld a,(ix+O_DX)
        neg
        ld (ix+O_DX),a
.mv:    ld a,(ix+O_X)
        add a,(ix+O_DX)
        ld c,a
        ld b,(ix+O_Y)
        call en_try
        jr c,.vy
        ld a,(ix+O_DX)
        neg
        ld (ix+O_DX),a
.vy:    ld c,(ix+O_X)
        ld a,(ix+O_Y)
        add a,(ix+O_DY)
        ld b,a
        call en_try
        ret c
        ld a,(ix+O_DY)
        neg
        ld (ix+O_DY),a
        ret

; Comportamento 2: persegue pelo eixo mais distante, contorna obstáculos
beh_chase:
        ld a,(ix+O_DTIM)
        or a
        jr z,.seek
        dec (ix+O_DTIM)
        ld a,(ix+O_X)
        add a,(ix+O_DX)
        ld c,a
        ld a,(ix+O_Y)
        add a,(ix+O_DY)
        ld b,a
        call en_try
        ret c
        ld (ix+O_DTIM),0
        ret
.seek:  ld c,(ix+O_X)
        ld a,(obj_player+O_X)
        call towards
        ld (ch_sx),a
        ld a,e
        ld (ch_ax),a
        ld c,(ix+O_Y)
        ld a,(obj_player+O_Y)
        call towards
        ld (ch_sy),a
        ld a,e
        ld hl,ch_ax
        cp (hl)                     ; distância y - distância x
        jr nc,.yfirst
        call .tryx
        ret c
        call .tryy
        ret c
        jr .stuck
.yfirst:
        call .tryy
        ret c
        call .tryx
        ret c
.stuck: call rnd
        and 3
        add a,a
        ld e,a
        ld d,0
        ld hl,dir_vec2
        add hl,de
        ld a,(hl)
        ld (ix+O_DX),a
        inc hl
        ld a,(hl)
        ld (ix+O_DY),a
        ld (ix+O_DTIM),12
        ret
.tryx:  ld a,(ch_sx)
        or a
        ret z
        add a,(ix+O_X)
        ld c,a
        ld b,(ix+O_Y)
        jp en_try
.tryy:  ld a,(ch_sy)
        or a
        ret z
        add a,(ix+O_Y)
        ld b,a
        ld c,(ix+O_X)
        jp en_try
dir_vec2:
        DB 0,0xFE, 0,2, 0xFE,0, 2,0

; Ponto de surgimento livre e longe do jogador -> carry = achou (X, Y gravados)
find_spawn:
        call rnd
        and 7
        ld (fs_i),a
        ld b,8
.l:     push bc
        ld a,(fs_i)
        add a,a
        ld e,a
        ld d,0
        ld hl,spawn_pts
        add hl,de
        ld c,(hl)
        inc hl
        ld b,(hl)
        ld a,(obj_player+O_X)
        sub c
        jr nc,.p1
        neg
.p1:    cp 48
        jr nc,.far
        ld a,(obj_player+O_Y)
        sub b
        jr nc,.p2
        neg
.p2:    cp 48
        jr c,.next
.far:   push bc
        call box_check16
        pop bc
        or a
        jr nz,.next
        ld a,(box_stairs)
        or a
        jr nz,.next
        ld (ix+O_X),c
        ld (ix+O_Y),b
        pop bc
        scf
        ret
.next:  ld a,(fs_i)
        inc a
        and 7
        ld (fs_i),a
        pop bc
        djnz .l
        or a
        ret
spawn_pts:
        DB 24,24, 216,24, 24,120, 216,120, 120,24, 120,120, 24,72, 216,72

en_kill:
        ld (ix+O_STATE),ES_DYING
        ld (ix+O_TIMER),8
        ld (ix+O_INK),POOF_INK
        jp en_poof_gfx

; ---------------- colisões ----------------
collide:
        ld ix,obj_en0
        ld b,MAX_EN
.l:     push bc
        ld a,(ix+O_STATE)
        cp ES_ALIVE
        jp nz,.n
        ld a,(obj_proj+O_STATE)
        or a
        jr z,.pl
        ld a,(obj_proj+O_X)
        add a,4
        ld b,a
        ld a,(ix+O_X)
        add a,8
        sub b
        jr nc,.a1
        neg
.a1:    cp 10
        jr nc,.pl
        ld a,(obj_proj+O_Y)
        add a,4
        ld b,a
        ld a,(ix+O_Y)
        add a,8
        sub b
        jr nc,.a2
        neg
.a2:    cp 10
        jr nc,.pl
        push ix
        ld ix,obj_proj
        call proj_kill
        pop ix
        dec (ix+O_HP)
        jr z,.dead
        call snd_tink
        jr .n
.dead:  ld a,(ix+O_TYPE)
        call etype_addr
        ld de,6
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        call add_score
        call en_kill
        call snd_kill
        jr .n
.pl:    ld a,(obj_player+O_X)
        ld b,a
        ld a,(ix+O_X)
        sub b
        jr nc,.b1
        neg
.b1:    cp 12
        jr nc,.n
        ld a,(obj_player+O_Y)
        ld b,a
        ld a,(ix+O_Y)
        sub b
        jr nc,.b2
        neg
.b2:    cp 12
        jr nc,.n
        ld a,(ix+O_TYPE)
        call etype_addr
        ld de,5
        add hl,de
        ld a,(hl)
        call lose_energy
        call en_kill
        call snd_hit
        ld a,2
        out (0xFE),a
        ld a,4
        ld (flash_cnt),a
.n:     pop bc
        ld de,OBJ_SIZE
        add ix,de
        dec b
        jp nz,.l
        ret

; ---------------- objetos ----------------
items_check:
        ld hl,items_def
        ld de,item_taken
        ld b,NUM_ITEMS
.l:     ld a,(cur_room)
        cp (hl)
        jr nz,.n
        ld a,(de)
        or a
        jr nz,.n
        push bc
        push hl
        inc hl
        ld a,(hl)
        add a,a
        add a,a
        add a,a
        ld c,a
        inc hl
        ld a,(hl)
        add a,a
        add a,a
        add a,a
        ld b,a
        pop hl
        ld a,(obj_player+O_X)
        sub c
        jr nc,.a
        neg
.a:     cp 12
        jr nc,.no
        ld a,(obj_player+O_Y)
        sub b
        jr nc,.b
        neg
.b:     cp 12
        jr nc,.no
        pop bc
        jr take_item
.no:    pop bc
.n:     inc hl
        inc hl
        inc hl
        inc hl
        inc de
        djnz .l
        ret

; HL = registro do objeto, DE = seu indicador "pego"
take_item:
        ld a,1
        ld (de),a
        push hl
        call draw_item              ; A = 1: apaga
        pop hl
        inc hl
        inc hl
        inc hl
        ld a,(hl)
        call kind_addr
        ld a,(hl)
        inc hl
        ld c,(hl)
        push hl
        or a
        jr nz,.e1
        ld b,c                      ; chave: keys |= 1 << valor
        inc b
        ld a,0x80
.k:     rlca
        djnz .k
        ld hl,keys
        or (hl)
        ld (hl),a
        jr .done
.e1:    dec a
        jr nz,.food
        ld hl,pieces
        inc (hl)
        jr .done
.food:  ld a,(energy)
        add a,c
        jr c,.max
        cp ENERGY_MAX+1
        jr c,.st
.max:   ld a,ENERGY_MAX
.st:    ld (energy),a
.done:  pop hl
        ld de,5
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        call add_score
        jp snd_pick

; DE = pontos em BCD (4 dígitos)
add_score:
        ld hl,score+2
        ld a,(hl)
        add a,e
        daa
        ld (hl),a
        dec hl
        ld a,(hl)
        adc a,d
        daa
        ld (hl),a
        dec hl
        ld a,(hl)
        adc a,0
        daa
        ld (hl),a
        ld a,1
        ld (hud_dirty),a
        ret

; ---------------- energia e vidas ----------------
energy_tick:
        ld hl,drain_cnt
        dec (hl)
        ret nz
        ld (hl),DRAIN_TICKS
        ld a,1
lose_energy:
        ld b,a
        ld a,(energy)
        sub b
        jr nc,.ok
        xor a
.ok:    ld (energy),a
        ret

player_die:
        call hud_energy
        call snd_die
        ld hl,lives
        dec (hl)
        ld a,1
        ld (hud_dirty),a
        jr nz,.cont
        ld a,2
        ld (game_result),a
        jp hud_lives
.cont:  ld a,ENERGY_MAX
        ld (energy),a
        ld a,(entry_x)
        ld (obj_player+O_X),a
        ld a,(entry_y)
        ld (obj_player+O_Y),a
        ld b,25
.p:     halt
        djnz .p
        ld a,(cur_room)
        jp enter_room

; =============================================================================
; Dados do motor
; =============================================================================
; Acentos U+00C0..U+00FF: letra-base da ROM + tipo de acento
; (0 nenhum, 1 agudo, 2 grave, 3 circunflexo, 4 til, 5 cedilha, 6 trema)
acctab:
        DB 'A',2, 'A',1, 'A',3, 'A',4, 'A',6, 'A',0, 'E',0, 'C',5   ; ÀÁÂÃÄÅÆÇ
        DB 'E',2, 'E',1, 'E',3, 'E',6, 'I',2, 'I',1, 'I',3, 'I',6   ; ÈÉÊËÌÍÎÏ
        DB 'D',0, 'N',4, 'O',2, 'O',1, 'O',3, 'O',4, 'O',6, 'x',0   ; ÐÑÒÓÔÕÖ×
        DB 'O',0, 'U',2, 'U',1, 'U',3, 'U',6, 'Y',1, 'P',0, 's',0   ; ØÙÚÛÜÝÞß
        DB 'a',2, 'a',1, 'a',3, 'a',4, 'a',6, 'a',0, 'e',0, 'c',5   ; àáâãäåæç
        DB 'e',2, 'e',1, 'e',3, 'e',6, 'i',2, 'i',1, 'i',3, 'i',6   ; èéêëìíîï
        DB 'd',0, 'n',4, 'o',2, 'o',1, 'o',3, 'o',4, 'o',6, '/',0   ; ðñòóôõö÷
        DB 'o',0, 'u',2, 'u',1, 'u',3, 'u',6, 'y',1, 'p',0, 'y',6   ; øùúûüýþÿ
; linhas 0 e 1 de cada acento (tipos 1..6; o 5 não usa)
acc_pat:
        DB 0x08,0x10, 0x20,0x10, 0x18,0x24, 0x34,0x58, 0x00,0x00, 0x00,0x24

blank_tile:
        DS 32,0

; ---------------- variáveis ----------------
cur_room:       DB 0
room_ptr:       DW 0
cur_fattr:      DB 0
ctrl_mode:      DB 0
energy:         DB 0
lives:          DB 0
score:          DB 0,0,0
keys:           DB 0
pieces:         DB 0
tick:           DB 0
drain_cnt:      DB 0
on_stair:       DB 0
entry_x:        DB 0
entry_y:        DB 0
facing:         DB 0
walk_cnt:       DB 0
input:          DB 0
moved:          DB 0
hud_en_drawn:   DB 0
hud_dirty:      DB 0
game_result:    DB 0
flash_cnt:      DB 0
bump_cd:        DB 0
fire_cd:        DB 0
seed:           DW 0x1234
last_frame:     DB 0
go_side:        DB 0
box_stairs:     DB 0
box_solid:      DB 0
ab_c1:          DB 0
ab_r1:          DB 0
ab_nc:          DB 0
fr_tile:        DB 0
fr_tile2:       DB 0
cr_attr:        DB 0
di_mode:        DB 0
di_attr:        DB 0
prow:           DB 0
pcol:           DB 0
pattr:          DB 0x47
pmargin:        DB 0
pdbl:           DB 0
acc_type:       DB 0
acc_base:       DB 0
title_page:     DB 0
beh:            DB 0
fs_i:           DB 0
en_type:        DB 0
en_count:       DB 0
ch_sx:          DB 0
ch_sy:          DB 0
ch_ax:          DB 0
sh_cnt:         DB 0
tile_ptr:       DS 22,0
tile_attr:      DS 11,0

objects:
obj_player:     DS OBJ_SIZE,0
obj_proj:       DS OBJ_SIZE,0
obj_en0:        DS OBJ_SIZE*MAX_EN,0

state_area:
link_open:      DS MAX_LINKS,0
item_taken:     DS MAX_ITEMS,0
STATE_LEN       EQU $-state_area

        ASSERT NUM_LINKS <= MAX_LINKS
        ASSERT NUM_ITEMS <= MAX_ITEMS
        ASSERT NUM_ITEMS > 0
        ASSERT NUM_SPR16*192 <= SHBUF8-SHBUF16
        ASSERT NUM_SPR8*64 <= CMAP-SHBUF8
        ASSERT ENERGY_MAX <= 128
