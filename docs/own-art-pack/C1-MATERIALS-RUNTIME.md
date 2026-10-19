# Piso e paredes próprios — aplicação local R7

Em 12/10/2026, o pedido humano de aplicar as texturas de paredes e de chão r1 foi concluído na prévia local `versions/diablo3d-preview-8496b3b7ee7c-materials-r7`. A execução foi transferida pelo principal após fechar a R6; Source e compilação tiveram um único responsável. A R6 e os masters permanecem preservados.

## Revisão qualificada

- EXE normal: `8496b3b7ee7ca4268531c008a69cfb35081e2c11879418c9bc970fc6468c19d7`.
- Pack: `5336b93ec2085f95fc4153a0f4c977aa8b9965dc21826f1feae03c132b2b4f23`, 1.448 entradas. Todos os 1.446 payloads da base R6 foram preservados; somente os dois materiais foram acrescentados.
- Manifesto da versão: `d3d03c81094559cfa82ccba8c919de507351897040c7ef99ff5d4c909574a0f7`. `VerifyOnly` conferiu 304 arquivos congelados sem iniciar o jogo.
- Source: `clean-base/devilutionx-c1-r1/Source/engine/render/town_view.cpp/.hpp`; hashes e objetos/link estão nos recibos privados. A build antiga permaneceu intacta; foram compiladas duas TUs e reutilizadas as bridges qualificadas.

O launcher é `Iniciar-Diablo3D.cmd` dentro da R7. O perfil é uma cópia independente do perfil sintético do gate, Speed32/FOV80; escolher Um jogador → NativeLoadGate → Carregar jogo. Nenhum perfil humano, launcher habitual ou publicação foi alterado.

## Recursos e luz

`s/materials/cathedral/floor/stone-r1` fornece um CLX opaco de 1 frame 128×128, solicitado antes do fallback por peça. A admissão nativa de piso, donor3 e metadados MIN permanecem. `s/materials/cathedral/wall/stone-r1` fornece um CLX opaco de 1 frame 64×128 apenas em Masonry+SolidMasonry. O raw retorna antes das courses/bevels nativas; o Bake de luz nativa continua ativo. Teto, madeira de portas, colunas originais, grades, arcos e seus operadores/recortes não recebem esse override. Faces de escadas já admitidas como SolidMasonry recebem o material, preservando a geometria.

O cold loader valida lista, dimensões antes do decode, cobertura e identidade dos pixels. FNV64 dos índices: piso `19fb0138bd10258b`, parede `7ceac50e7ad3f981`, vinculados aos CLXs/masters por SHA256. A assinatura inclui revisão e presença/conteúdo qualificado; o reset existente descarta raw/lit e presença. Alterar pack no mesmo processo exige reset do provider e dos recursos; simples cópia não é hot reload. O orçamento conjunto continua 8MiB, 512 entradas raw e 512 lit, sem decode ou leitura em warm draw. Ausência dos dois recursos conserva os fallbacks anteriores; recurso inválido é recusado, sem mascará-lo como ausência.

## Evidência e limites

Gate próprio **PASS125186**: cada piso e parede opaca admitidos compara o vetor inteiro com o CLX selecionado, renderizado independentemente pelo renderer nativo. CPU e hardware executaram iluminação uniforme/per-pixel; warm draw manteve identidades sem decode/miss/IO; bytes raw+lit selecionados respeitaram o orçamento. Luz uniforme controlada 0/8 correspondeu às LightTables nativas. Reset renovou identidade preservando conteúdo. Triângulos/picks e testemunhos de mapa/SOL/atores/loot/RNG permaneceram. Baú R6, barril/fragmentos, cadáveres, caixão, logo e HUD passaram novamente. O mesmo gate/Source com o pack R6 sem os materiais passou **111634** checks de fallback. Isso não é prova de FPS prolongado nem equivalência completa de framebuffers/per-pixel entre backends.

Recibos, logs, capturas e auditoria independente: `diagnostics/own-art-c1-material-apply-20261012-r1/`. A tentativa inicial de link excedeu o limite de comando do Windows; a reparação consumiu os mesmos arquivos de resposta diretamente no MSVC LINK, mantendo os objetos compilados, flags e manifestos. As falhas e seus logs foram preservados, sem relaxar asserts.

São **LODs indexados**, não RGB/PBR em 1254px: piso 128²; parede usa duas bandas idênticas 64² em 64×128. Os UVs atuais dão 1×1,4 unidades por banda; a proporção no raster não prova isotropia física. As junções originais continuam; não houve reparo de bordas nem seleção automática da parede r2. Os masters RGB1254², hashes selecionados e ZIP anterior permanecem intactos. Códigos 0/128–254 são comuns às quatro paletas L1 e Town; nenhuma paleta ou pixel nativo foi incorporado pelo conversor.

Escopo qualificado: Tristram/Catedral nível1 na revisão local. Demais andares, teto mais alto, arcos novos e animações próprias continuam com suas pendências anteriores. Aceite visual humano e promoção habitual/pública não são anunciados por este gate.
