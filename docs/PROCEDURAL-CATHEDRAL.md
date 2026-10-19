# Piloto privado dos níveis procedurais: Catedral

Iniciado em 8 de outubro de 2026 por pedido explícito de Douglas, antecipando um piloto G3 em paralelo à revisão de Tristram. G1/G2 permanecem parciais. Este documento detalha a frente reservada no [guia operacional](PROJECT-EXECUTION.md); não substitui sua fila nem autoriza integrar, instalar ou anunciar um andar jogável.

O primeiro incremento já executa o gerador nativo em um processo privado, captura seu estado e monta geometria técnica modular. **Não há outro gerador de salas. Não há integração do adaptador ao renderer da partida nesta entrega.** Fontes produtivas, CMake, cache de build, perfis, launchers, Git e o guia operacional continuam com o principal.

## Reserva e fontes candidatas

Raiz local, fora do repositório público: `D:/Diablo 1 3D/diagnostics/procedural-cathedral-20261008/`. O `manifest.json` reserva arquivos, responsáveis e limites antes da escrita. Base auditada: `0753508d3a60f25af2fab51694404cfd57063041`.

| Arquivo privado | Responsabilidade |
| --- | --- |
| `candidate/cathedral_snapshot.hpp` | Snapshot congelado, coordenadas e vínculos nativos |
| `candidate/native/cathedral_native_capture.{hpp,cpp}` | Leitor do estado vivo; sem geração, comandos ou RNG |
| `candidate/cathedral_adapter.{hpp,cpp}` | Malhas técnicas reutilizáveis, instâncias, cache regional, bounds e referência CPU de raios |
| `candidate/cathedral_adapter_smoke.cpp` | Testes sintéticos e consumo dos snapshots nativos |
| `candidate/native/native_fixture.cpp` | Harness privado do gerador e das operações reais de portas |
| `tools/run_native_fixture.ps1` | Congela objetos/bibliotecas existentes por SHA e linka outro executável privado |
| `tools/run_adapter_fixture.ps1` | Compila somente o adaptador privado; fixa ambos headers, CPPs e receita por SHA |
| `tools/run_review.ps1` | Exportação glTF, ilustração CPU e salvar/reabrir cenas Godot privadas |
| `tools/build_coverage.py` | Inventário de IDs, salas, corredores, portas e escadas das capturas existentes |

`audit/native-map.md` e `audit/render-contract.md` contêm referências de código e hashes. `reports/handoff.json` congela fontes, headers, receitas, recibos e patch de arquivos novos; não aplicar esse patch sem integração pelo principal. O harness reaproveita **objetos já compilados**, copiados com verificação antes/depois, não recompila o jogo inteiro a partir das fontes atuais. As identidades dos inputs estão em `fixtures/native/build-inputs.json`; a compilação de integração ainda deve conferir sua base.

## Snapshot V2

Capturar na thread proprietária da simulação, depois de concluir suas alterações e antes de preparar o frame. `CaptureMetadata.gameRevision` é uma época não nula fornecida pelo ciclo de carga nativo, renovada ao gerar, carregar, trocar ou recarregar um mapa. Seed e número do andar não são identidades suficientes para reutilizar slots de objetos.

O leitor registra seed de proveniência, estado atual do LCG, época, nível/entrada, `pOriginalCathedral`, `fullQuests`, edição, `leveltype`, `setlevel`, `setlvlnum` e `dminPosition/dmaxPosition`. Este piloto aceita somente Catedral normal, nível 1, sem Hellfire/setlevel, `SL_NONE`, limites ativos `[16,16]..[96,96)` e época válida. Incompatibilidades são recusadas, inclusive outro tipo de mapa que também tenha `currlevel=1`.

O snapshot contém os 1.600 megatiles de `dungeon`; 12.544 células com `dPiece`, byte SOL efetivo, setor `dTransVal`, `dSpecial`, luz, flags e ocupação assinada `dObject`; tabela MIN/micros; `TransList`; portas L1 ativas com slot, orientação, estado, seleção, frame e peças originais; e triggers nativos. Não é ainda um contrato geral de monstros, heróis, itens e todos os objetos de cenário.

Arrays congelados são explicitamente row-major `z*width+x`, convertidos dos arrays nativos `[x][y]`. Godot usa `(X,Y,Z)=(x,altura,y nativo)`, uma unidade por célula. Megatile `(mx,my)` começa em `(16+2*mx,16+2*my)`. IDs de `dungeon` são 1-based; `dPiece` é zero-based; `_oVar1/_oVar2` das portas preservam o valor salvo 1-based. O acessor valida ambos eixos antes de achatar o índice.

Formato privado textual: `D3D_CATHEDRAL_SNAPSHOT 2`; linha de contexto com 15 campos (`seed rngState gameRevision level entry originalCathedral fullQuests hellfire levelType setLevel setLevelId minX minZ maxX maxZ`); 1.600 megatiles; 12.544 registros de sete campos; contagem/tabela de micros; 256 flags de setores; contagem/portas de nove campos; contagem/triggers de quatro campos. O leitor limita contagens, IDs, enumerações e coordenadas, recusa truncamento/conteúdo extra e confere ocupação da porta contra seu slot.

O JSON de cena, glTF e TSCN são **produtos visuais parciais**, não substitutos do snapshot. Não carregam todos os arrays SOL, visão, luz e micros. Integração, reprodução e futuras comparações de colisão precisam do snapshot V2 companheiro e de seu recibo. Triggers preservam `_tlvl` bruto, que nesta fixture é zero mesmo ao descer; interpretar destino exige `_tmsg` e nível atual.

## Módulos e montagem técnica

Há 14 protótipos imutáveis no namespace local `technical.cathedral.*`. Esses nomes não são reservas de arte final no registry. Cada instância conserva ID determinístico, transformação, bounds completos, megatile/piece de origem, setor e vínculo nativo de seleção. O registry atual continua sendo a fonte única de futuros IDs/revisões artísticos; nenhuma seleção aceita foi alterada.

| Módulo | Derivação atual e dimensões técnicas |
| --- | --- |
| Chão | Quad de 1×1 centrado na célula; somente regiões livres do SOL, com recorte visual provisório da descida |
| Parede | Fronteira entre espaço e SOL sólido, segmento de 1 unidade, espessura 0,18 e altura 2,8 |
| Canto, final e encontros 3/4 | Grafo de endpoints em meia célula; biblioteca preparada. As capturas reais usam cantos, sem comprovar encontro 3/4 |
| Vão/porta | Moldura com abertura central de 1 e altura livre 2,25; folha separada de 1×2,2 vinculada ao objeto ativo |
| Escada acima/abaixo | Âncoras explícitas 64/59 dos minisets nativos; oito degraus de 0,5×0,3, largura 2; destino vinculado ao trigger |
| Arco | Somente raw tiles 11/12, módulo de duas unidades, orientação explícita |
| Pilar | Somente raw tile 15, base técnica de 0,5×0,5 |
| Grade | Somente raw tiles 35/36; duas unidades, altura 1,5 |
| Marcador nativo | Protótipo disponível; não usado como substituição silenciosa dos IDs sem regra |

**A fronteira SOL não fornece a geometria artística original.** Não revela alturas, cantos completos, encontros, pilares, grades ou escadas. `BaseTypes` foi feito para sombra e colapsa portas/grades em outros tipos; não é classificação suficiente. O chão técnico quadrado ainda difere dos offsets de inversão do raster original usados em `town_view`. A fidelidade de sockets, orientação/altura de cada peça, arte e passagens em 360° permanece pendente.

O inventário `reports/fixture-coverage.json` lista cada ID presente, contagem e primeira coordenada. Nas duas seeds há 91/87 IDs raw distintos; 84/81 não têm regra geométrica dedicada além de sua participação na fronteira SOL. Âncoras explícitas de arcos/pilares/grades/escadas são sete IDs; portas derivam dos objetos vivos. Isso não significa cobertura artística integral. O inventário de produção deve agrupar os demais IDs por estrutura nativa, preservando revisões e sem gerar modelos por simples contagem de tile.

As folhas abertas podem coincidir parcialmente com a parede aproximada de SOL. Os raios nativos deste incremento amostram a porção externa exposta da folha e provam vínculo/estado, **não** o encaixe inteiro da porta. Um raio pelo centro da folha esquerda aberta da seed 2588, porta `(38,25)`, encontrou primeiro a parede técnica. Esse limite está registrado, sem promoção a fidelidade. A solução futura pertence à classificação/encaixes das paredes e portas, não à colisão nativa.

## Estado vivo e cache

`Adapter::Update(const Snapshot&)` não escreve no mapa nem chama RNG. A planta é dividida em 100 regiões de 8×8 células. Malhas de módulo são compartilhadas; regiões imutáveis são reutilizadas por assinatura. Não existe uma malha única do andar.

A assinatura de geometria lê pieces/SOL/setor/especial/ocupação, raw megatiles, portas e um halo de duas células. Portas podem alterar também o vizinho: esquerda `(0,-1)`, direita `(-1,0)`. Abrir/bloquear/fechar é estado nativo, e estado bloqueado permanece visualmente aberto. Mudanças na ordem da lista de triggers invalidam vínculos que usam seu índice; mudança de escadas invalida globalmente as raras alterações dessa topologia, incluindo seu recorte de chão. Nova época invalida todas as regiões. Bounds incluem folha aberta, rotação, altura e descida fora da região proprietária.

Luz, flags de visão e `TransList` têm assinatura de apresentação separada e não remontam geometria estática. **Capturar esses dados não implementa sua política visual.** O renderer futuro ainda deve tratar `Transparent`, `TransparentLeft/Right`, setor ativo, oclusão e picking de forma coerente. Culling aqui é somente a referência CPU conservadora por AABB; não foi ligado ao frustum/GPU da partida.

## Fixtures e execução comprovada

| Fixture | Cobertura real |
| --- | --- |
| Seed 2588, MAIN/PREV | Mapa/transparência exatos; 8 portas, ambas orientações; 2 triggers; salas/corredores/canto/grades/pilares e ambas escadas |
| Seed 743271966, MAIN/PREV | Mapa/transparência exatos; 4 portas direitas, nenhuma esquerda; 2 triggers; planta e posições diferentes |
| Transições | 22 snapshots iniciais/aberto/bloqueado/fechado. Porta de fronteira exata existe nas duas seeds, não foi criada artificialmente |

Os mapas são comparados às fixtures existentes `test/fixtures/diablo/1-2588.dun` e `1-743271966.dun`: 1.600 megatiles e 6.400 setores ativos exatos por caso, mais viewpoint. `l1.til`, SOL corrigido pelo loader e MIN reais são carregados do arquivo local do jogador. Nenhum extrato ou snapshot pertence ao Git.

Executado nesta entrega:

- Harness nativo R3: **308.170 verificações em cada uma de duas execuções**, 22 snapshots idênticos por SHA, 19 rejeições de contexto/estado inválido. Geração real e `SyncOpObject` real abrir→bloquear→fechar preservaram RNG e restauraram mapa/portas/triggers. Corpos dos snapshots V2 também coincidiram com os dados arquivados da R2.
- Adaptador C++: **1.267.379 verificações**, compilação privada MSVC C++20, `/W4 /WX`. Malhas finitas/não degeneradas, IDs, bounds/culling, cache, seleção, invalidação de fronteira, epoch, truncamento, variantes/bounds/ocupação inválidos e raios NaN/Inf/degenerados. Os 22 snapshots reais foram consumidos; transição individual de porta invalidou no máximo quatro regiões, retorno nativo fechado reutilizou a baseline.
- Godot privado: salvar/reabrir as duas cenas com 100 regiões, **4.423/3.986 instâncias** e dez malhas efetivamente usadas compartilhadas. Metadados nativos preservados; sem render Godot/GPU nem aplicação ao jogo.
- glTF privado: 14 malhas compartilhadas e hierarquia por região/instância. Duas ilustrações CPU dos triângulos, identificadas como técnicas, foram geradas e inspecionadas. Não são capturas do jogo.

Limite da inicialização nativa: `CreateDungeon` + `CreateThemeRooms` + `AddL1Objs` + `InitL1Triggers`. Não inclui `InitObjects` completo, monstros, simulação de combate, save/load, visão dinâmica do jogador ou comandos na partida habitual. Os testes de raios sobre portas capturadas provam a cena técnica; o dispatch do cursor nativo e a interação física ainda não foram exercitados. Comparação exata de SOL capturado não é teste completo de `IsTileWalkable/CanStep` e diagonais.

As receitas e recibos locais permitem repetir sem usar CMake/Ninja nem trocar o executável instalado. O smoke do adaptador reserva 8 MiB de stack apenas para manter várias cópias integrais de snapshots nos testes; nenhum flag de runtime foi alterado. A receita nativa depende dos objetos/bibliotecas já presentes e falha se faltarem ou mudarem durante a cópia.

## Próxima integração, sob responsabilidade do principal

Consolidar a fronteira mínima de cena a partir deste segundo adaptador, reutilizando câmera, BVH, GPU, culling e composição existentes. Apenas liberar o gate de `town_view` seria incorreto: objetos/monstros, seleção e transparência dos andares ainda precisam de contratos próprios.

Antes de habilitar qualquer nível na partida: integrar captura ao ciclo de carga/estado vivo, vincular IDs ao cursor/comandos nativos, respeitar luz/visão/transparência, preservar publicação atômica de cor/profundidade/picking, invalidar caches por época/estado, validar CPU/GPU/câmeras/F4/Home, portas/escadas/diagonais e save/load com mapa/RNG/colisão idênticos. Medir custo/memória nesse caminho real e revisar encaixes/arte contra o backend original forçando geometria e em 360°. Esta entrega não altera seed, saves, navegação, quests, combate ou opções do jogador.
