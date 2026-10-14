# Iluminação contínua do mundo 3D — R14

Pedido humano de 12/10/2026: trocar limites em quadrados/faixas por um degradê suave no término da luz, em todo o renderer 3D atual. Reabre somente a apresentação da luz; não regenerar os modelos e texturas já recebidos.

## Resultado

A opção gráfica **Smooth 3D Lighting**, ativada por padrão, aplica no próximo frame e persiste pelo sistema normal de configurações. O mundo usa composição ARGB8888 independente do HUD. O renderer continua produzindo a imagem indexada original de recuperação, os mesmos IDs de seleção e a mesma profundidade. Desativar a opção mantém o caminho anterior. Tristram e a Catedral normal foram qualificados; outros andares/biomas não ganham uma qualificação completa por associação.

Luz nativa da Catedral: leitura visual bilinear do campo dLight por posição de cada pixel, interpolação das cores entre os níveis de luz. A cópia de albedo indexado original das paredes é mantida junto da textura nativa já iluminada: RGB ilumina a base original, sem iluminar duas vezes a imagem baked. Imagem indexada continua usando Bake e LUTs nativos. Prop/luz, seleção, exploração, SOL, RNG e saves permanecem sob autoridade nativa; raio/posição das luzes nativas não foram mudados.

Tristram: intensidade contínua nos LUTs de luz direcional e fontes interiores; interpolação entre amostras do PCF na penumbra, respeitando o plano da superfície receptora. CPU e GPU calculam a mesma transição. Céu e névoa têm cor/depth contínuos na camada RGB, sem desfocar texturas ou inserir dithering. Modos Dia/Entardecer/Noite R11 continuam. As fontes pontuais já têm janela radial quadrática que chega suavemente a zero; agora o resultado não é reconvertido ao framebuffer de 256 cores na apresentação.

Não é um renderer PBR completo nem substituição dos masters/albedo por RGB pleno: imagens nativas, bases e LUTs continuam com as limitações da origem. Resolução/granulação das texturas e a semântica dos blends de magia nativos permanecem.

## Donos e lifecycle

- `GraphicsOptions.smooth3dLighting`: preferência; init/save/load nativos, nunca resetada por troca de nível.
- `town_view.cpp`: prepara paleta de brilho, campo visual e pixels RGB; limpa validade no início de cada frame. Regiões incompletas/refusadas não publicam RGB antigo. API histórica GetTownViewHighResolutionFrame também oferece frame RGB1x, além do antigo 2x indexado.
- `town_gpu.cpp/.hpp`: quarto target ARGB empacotado acompanha os três targets originais. SetSmoothLighting só é aceito depois de Begin e antes da primeira geometria. Campo finito 0..15, dimensões até128²; paleta256. Recurso/identidade, dimensões e dados são cacheados. Begin inicia a transação; End publica RGB com depth/IDs/índices ou recusa o frame; reset libera recursos. RGB readback é opcional quando o modo está desligado.
- `town_gpu_mesh_backend.inc`: mesmos recursos RGB/field/base ligados para triângulos projetados e meshes residentes; nenhum novo layout de vértices ou deformação.
- `town_presentation.cpp`: composição de mundo RGB exato1x/2x e UI por cobertura independente de cor. Pixels pretos do HUD permanecem opacos. Fades de paleta são aplicados na apresentação mesmo sem reconstruir geometria. Rotações da paleta em frames normais são conservadas.

Budget de cor: máximo4.194.304pixels; dois buffers RGBA de4bytes por pixel, além dos recursos existentes. Canais originais das paredes entram nos mesmos limites de8MiB de cache nativo e256MiB de cache GPU, incluindo contabilização/evicção. Campo/paleta auxiliares têm dimensões finitas. Falha das alocações opcionais de cor preserva a imagem indexada utilizável; não ampliar budgets para esconder erros.

O relógio visual das portas/baús continua SDL em jogo normal. Uma injeção de tempo diagnóstica permite capturar meio/final independentemente de frame frio ou tempo de PNG. Negativo/reset restaura SDL; não altera tick nativo, Speed32 ou duração350/600ms.

## Evidência e entrega

EXE **77996c8571df**, prévia local `versions/diablo3d-preview-77996c8571df-smooth-lighting-r14`,436arquivos VerifyOnly. Todos435assets iguais à R13; perfil sintético separado Speed32/1280x720/terceira pessoa/entardecer/luz suave ON. Tristram inicial; entrar na Catedral para testar.

Gate final **PASS125731**: três fases RGB Town, C1 GPU/CPU, mais256cores na camada real (1291), erro médio de canal CPU/GPU0.020838 em C1 e0.039767 em Town; plano controlado supera240intensidades e maior salto adjacente<=2/255. Pick ownership preservado em cada amostra. Imagem indexada completa igual com opção ligada/desligada. Compositor SDL real aceita RGB1x/HUD e preserva retângulo preto por cobertura; fade fica totalmente preto sem redraw. Cache/budget/reset e portas/baús/props/HUD/Pepin/saveLoad R13 foram retestados. Contagem de asserts não é FPS sustentado nem aceite artístico.

Capturas `diagnostics/preview-native-20261012-r1/run-268892432071200/SMOOTH-*.png` são da camada RGB real ou compositor. Os PNGs indexados históricos não são usados para alegar a qualidade RGB. Source exata, recibo e logs em `diagnostics/smooth-lighting-20261012-r14/source-r14` e `DELIVERY.json`.

Falhas intermediárias preservadas: reset indevido do flag RGB entre gravação/readback (corrigido para Begin), flutuação do teste de meio da porta devido ao custo frio/PNG (tempo diagnóstico determinístico, asserts mantidos), e reparo de formatação de diretivas na compilação. Os candidatos intermediários não foram entregues. Revisão humana e FPS sustentado pendentes; sem instalação habitual, web, Git/Panvia ou publicação de arte/binário. README humano e revisões anteriores intactos.
