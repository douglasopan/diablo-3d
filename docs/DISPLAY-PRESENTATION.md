# Apresentação do Diablo 3D: resolução, nitidez e interface

O D3D pretende transformar todo o Diablo 1 em 3D, incluindo os níveis procedurais. A resolução e a apresentação já podem ser comparadas usando os recursos do DevilutionX. A nova entrada, o novo menu e a separação completa entre escala da interface e renderização do mundo continuam como etapas próprias de desenvolvimento.

## Cabana e apresentação na partida habitual

O iniciador normal `Iniciar-Tristram.cmd` usa a mesma cabana selecionada dos perfis de revisão. No workspace local, a opção `3D Edge Smoothing` foi ligada uma vez no perfil habitual 960×540, preservando seu save e as demais preferências. Ela continua ajustável em Gráficos; instalações novas mantêm o padrão desligado por causa do custo. Não é necessário abrir uma cópia de personagem para reunir modelo e qualidade.

Com a opção ativa e o mundo efetivamente em 2×, o novo compositor SDL preserva essa imagem até a saída física. A câmera e a área visível permanecem iguais: em uma saída maior, o mesmo objeto pode conservar mais detalhe, em vez de receber apenas a ampliação da imagem lógica já reduzida. O mundo continua rasterizado na CPU e limitado à paleta; nenhum pacote de texturas do Belzebub foi importado.

A interface é composta por cima usando regiões explícitas de desenho no quadro lógico final. Preto permanece opaco, painéis em cache são cobertos e o cursor tem regiões transitórias próprias. O filtro do mundo segue a preferência gráfica; a camada da interface usa vizinho mais próximo para evitar halos nas bordas transparentes. Isso não acrescenta fontes/ícones HD nem escala independente à interface herdada.

Este primeiro compositor é conservador: dentro do retângulo de uma letra, sprite ou painel transparente, o fundo já composto também permanece na resolução lógica. Ao exceder 4096 regiões por canal, a cobertura recua ao quadro inteiro. A saída de superfície sem renderer SDL, SDL1, Home, mundo em 1× e falhas opcionais de criação/composição usam a apresentação anterior. Carregamentos, vídeos e menus fora da partida não recebem uma imagem 3D antiga.

O ganho depende da saída física: uma janela com o mesmo tamanho lógico não exibe quatro pixels físicos por pixel lógico. A amostragem continua cara na CPU; este incremento não transforma o rasterizador em GPU nem estabelece uma meta de FPS.

Validação local em 8 de outubro de 2026: jogo e diagnóstico compilados; fixtures sintéticas do compositor SDL passaram, incluindo detalhe físico pixel a pixel, preto opaco, paleta, clipping, cursor sem rastros, troca de renderer, resize e escala fracionária sem halo. A revisão do cenário real e a suíte de regressão também passaram, com 75 amostras geométricas independentes sem falhas. O mundo 2× da cabana foi exportado e inspecionado fora da janela. Não foi possível observar automaticamente a interface na janela do jogo: a ferramenta de controle falhou antes de abrir os aplicativos. Isso permanece uma revisão visual a fazer na partida.

```powershell
.\build\town_view_smoke.exe --presentation-layers '.\diagnostics\presentation-layers-synthetic'
```

Esse modo usa apenas imagens sintéticas e um renderer SDL de software, sem arquivos do Diablo. As evidências locais desta entrega estão em `diagnostics/presentation-layers-synthetic-20261008`, `diagnostics/presentation-layers-quality-gog-20261008` e `diagnostics/presentation-layers-regression-gog-20261008`. A medição de CPU do mundo em 960×540 foi 44,0 ms em 1× e 162,4 ms em 2×, excluindo a composição SDL; não é FPS de uma partida nem medição controlada definitiva. A preparação final do perfil normal preservou seu INI já ajustado e o save, verificou modelo/luz e renovou o recibo. Normal e alias local de qualidade receberam o mesmo binário atualizado.

## Comparar agora

Abra `Comparar-Apresentacao.cmd` e escolha uma opção. Para incluir a cabana Meshy disponível localmente, use:

```powershell
.\Comparar-Apresentacao.cmd -MeshyReview
```

Também é possível selecionar diretamente:

```powershell
.\Iniciar-Tristram.ps1 -Presentation Nitido -MeshyReview
.\Iniciar-Tristram.ps1 -Presentation Suave -MeshyReview
.\Iniciar-Tristram.ps1 -Presentation Amplo -MeshyReview
```

O modelo Meshy não acompanha o repositório público. Todos os modos usam a mesma [baseline selecionada](ASSET-APPROVAL.md) quando o arquivo local está disponível, inclusive o iniciador normal e os perfis de apresentação. `-MeshyReview` exige essas fontes locais e usa o perfil de revisão separado; sem a opção e sem o modelo, o iniciador anuncia o fallback procedural. A seleção não representa aceitação artística integral.

| Opção | Área interna | Ampliação | O que comparar |
|---|---|---|---|
| Nítido | 960×540 | Sem suavização (`Scaling Quality=0`) | Pixels definidos; serrilhado pode ficar mais visível em escalas fracionárias. |
| Suave | 960×540 | Filtro de qualidade 2 do SDL | Suavização na saída, conforme o renderizador de apresentação disponível. |
| Campo ampliado | 1280×720 | Mesmo filtro de Suave | Mais cenário; objetos e interface ficam menores na mesma tela. O trabalho do renderizador pode aumentar. |

Suave reproduz o filtro que já estava configurado no perfil local analisado. Nítido permite comparar outro método de ampliação; Campo ampliado muda a área de jogo. Nenhuma opção importa código ou arte do Belzebub.

Cada opção cria um `perfil-apresentacao-*` separado, com uma cópia inicial da configuração e dos saves locais de personagem. O progresso dessa cópia fica separado. Selecionar a opção novamente reaplica suas chaves gráficas; as demais opções do perfil são preservadas. Com `-MeshyReview`, a cópia inicial vem do perfil de revisão quando ele existe, e o modelo local é instalado nessa cópia.

O iniciador informa se faltam dados ou o modelo solicitado. `-PrepareOnly` prepara o perfil sem iniciar o jogo. Os perfis de comparação são ignorados pelo Git, assim como saves, modelos e extrações privadas.

F4 alterna a vista em Tristram; o botão do meio gira/inclina a câmera, a roda ajusta o zoom e Home restaura a vista original. Nas outras regiões, o renderizador original permanece ativo. Compare o mesmo objeto, estado e câmera ao avaliar nitidez; um campo de visão maior não é uma comparação de detalhe equivalente.

## Suavizar o contorno do mundo 3D

Em **Configurações → Gráficos → Suavização de bordas 3D** (`3D Edge Smoothing`), é possível ativar uma amostragem maior somente para o mundo reconstruído. A opção começa desligada. Ela mantém câmera, enquadramento, menus e coordenadas do mouse; não aumenta a área visível como o perfil Campo ampliado.

Para uma revisão isolada, abra `Revisar-Qualidade3D.cmd`. A cabana local selecionada é usada automaticamente; `-MeshyReview` também pode ser passado para exigir suas fontes. O iniciador cria uma cópia em `perfil-apresentacao-suave-qualidade[-meshy]` e ativa essa opção somente nela. Saves e preferências do perfil habitual são preservados; o progresso da cópia permanece separado. Se existir `build/devilutionx-tristram-quality.exe`, a revisão usa esse candidato; caso contrário, usa o executável normal compilado com o código atualizado. Isso permite testar uma versão separada quando o Windows mantém o executável habitual bloqueado por uma partida aberta.

O mundo é desenhado em duas vezes a largura e a altura. Cada grupo de quatro amostras também é reduzido a um pixel lógico para a composição herdada e o fallback. A redução usa cores RGB e uma tabela de aproximação à paleta, preservando índices uniformes. Ela não calcula médias dos números dos índices. Na saída SDL em camadas, o buffer 2× é preservado e convertido usando a paleta ativa, incluindo fades. Essa suavização pode reduzir serrilhado e mudar detalhes de alto contraste; não cria novas texturas ou novos modelos. A iluminação existente e o mapa de sombras continuam em coordenadas do mundo.

Para seleção, arquitetura e profundidade, a redução escolhe a mesma subamostra visível mais próxima. Uma borda que ocupa somente parte do pixel pode, portanto, selecionar o objeto da frente. A câmera, a colisão e os tiles nativos continuam autoritativos. Alterar a opção, o zoom nativo, os painéis ou as dimensões da tela invalida a seleção antiga até o próximo desenho.

O caminho Home permanece no renderizador original, sem essa redução. HUD e painéis são desenhados depois do mundo, com sua resolução habitual. A opção afeta por enquanto a reconstrução de Tristram; os outros níveis continuam no backend original.

Há um orçamento de **4.194.304 amostras** para o buffer ampliado. Áreas lógicas até 1.048.576 pixels podem usar 2× por eixo; acima disso, a opção recua para 1× e o HUD informa o limite. Por exemplo, 960×540 e 1280×720 cabem; 1920×1080 recua. Uma falha de criação da superfície SDL também recua. A recuperação de falhas do alocador C++ depende do suporte a exceções da compilação; não é uma garantia contra falta geral de memória.

Quatro amostras por pixel exigem mais processamento. Por isso a opção é experimental e desligada por padrão. O diagnóstico `town_view_smoke ... --quality` mede seu custo e verifica o retorno 1× → 2× → 1×, seleção por subpixels, dimensões ímpares, limites do viewport e comparação nativa. As capturas ficam locais e contêm somente o mundo; não representam FPS finais nem uma observação da interface na janela.

Na rodada de 7 de outubro de 2026, a suíte normal e a revisão de qualidade passaram, incluindo 75 amostras geométricas independentes sem falhas. Em 960×540, as medianas de CPU foram 41,8 ms em 1× e 136,9 ms em 2×, aproximadamente 3,27 vezes o custo. Havia duas instâncias do jogo abertas; esses tempos não demonstram desempenho sustentado. As imagens mostram uma melhoria sutil de contorno e minificação, com a suavização esperada de detalhes de alto contraste. O candidato do jogo foi compilado separadamente como `devilutionx-tristram-quality.exe`; depois que as partidas foram encerradas, o v4 habitual também recebeu os mesmos objetos já validados.

## O que foi aprendido com o Belzebub

O [registro das bases e créditos](ENGINE-BASES-AND-CREDITS.md) reúne as fontes oficiais e distingue o código realmente usado das referências estudadas. A [auditoria dos recursos](REFERENCE-SOURCES.md) registra a arte encontrada e seus limites.

A instalação fornecida contém SDL2, OpenGL/GLEW e recursos de interface próprios. A [engenharia reversa do executável](BELZEBUB-BINARY-ANALYSIS.md) examina as referências e os argumentos das chamadas gráficas, além das strings: confirma contexto OpenGL, alocação de texturas, filtragem linear, composição por alfa e um caminho de brilho. O estudo estático registra os limites de cada achado. A ferramenta de controle de aplicativos falhou antes de abrir o mod; não houve observação da sua tela em execução nesta etapa.

O atlas local `ctrlpan/modernui.png`, de 1024×1024, contém globos, barra de ações, botões e painéis de atributos. É evidência de arte adicional de interface. Sua dimensão é a de um atlas composto, não a resolução de uma casa ou personagem. Nenhuma imagem desse atlas foi incorporada ao D3D ou publicada como asset.

## Limites e próxima evolução

O nosso renderizador 3D atual desenha geometria na CPU e converte o resultado para a paleta do jogo. A apresentação SDL preserva o mundo 2× quando disponível e compõe regiões da interface lógica; o caminho herdado amplia o quadro final único. Aumentar somente `Width` e `Height` aumenta a área visível: a projeção nativa mantém sua escala em pixels. Isso não faz o mesmo objeto ganhar automaticamente mais amostras na tela, e os menus herdados não têm um controle geral de escala independente.

O experimento `town_view_smoke --presentation` produz capturas e tempos do renderizador fora da janela do jogo. Ele compara diferentes áreas internas com a mesma escala de projeção e preserva o estado nativo. Os resultados medem esse experimento de CPU; não são FPS finais do jogo, uma validação da saída SDL/monitor, nem uma medição do Belzebub.

```powershell
.\build\town_view_smoke.exe 'C:\caminho\do\Diablo' '.\build\assets' '.\diagnostics\presentation' --presentation
```

Na execução local de 7 de outubro de 2026, com a cabana Meshy, foram medidos três aquecimentos e cinco desenhos por resolução, com animação e fogo congelados:

| Área interna | Mediana do mundo original | Mediana do mundo 3D | Faixa dos cinco desenhos 3D |
|---|---|---|---|
| 960×540 | 0,825 ms | 44,897 ms | 44,511–45,349 ms |
| 1280×720 | 1,312 ms | 74,868 ms | 74,410–76,252 ms |
| 1920×1080 | 2,624 ms | 156,459 ms | 152,168–782,228 ms |

A medição inclui a limpeza diagnóstica e o desenho do mundo, excluindo exportação de PNG, leitura de picking e comparação de pixels. A máquina tinha outros trabalhos em andamento; as amostras de 1080p variaram bastante. Esses cinco desenhos não demonstram desempenho sustentado. A rodada sem importação apresentou a mesma tendência, sem estabelecer uma diferença significativa causada pela cabana.

As capturas confirmam a escala de um passo de tile em `[32,16]` pixels nas três resoluções, com mais mundo aparecendo no canvas maior. As verificações de repetição, limites do framebuffer, estado nativo e seleção geométrica passaram. O painel continua com 640×128 pixels; nas resoluções largas o mundo é desenhado também atrás da área que o painel sobrepõe. As imagens exportadas por este experimento contêm somente o mundo, sem a interface final.

Os resultados locais ficam em `diagnostics/presentation-native-gog` e `diagnostics/presentation-meshy-gog`. O executável do jogo e o modelo foram preservados; apenas a ferramenta diagnóstica foi recompilada. O custo observado justifica manter a resolução maior como comparação opcional e tratar renderização acelerada e escala independente da interface como trabalho próprio.

As próximas mudanças de apresentação devem:

1. Definir o enquadramento do mundo, sua densidade de pixels e a escala legível da interface separadamente, inclusive em 4:3, 16:9 e ultrawide.
2. Medir uma saída 3D com mais detalhe no **mesmo enquadramento**, materiais adequados e cores que mantenham o aspecto aprovado. Avaliar um caminho gráfico acelerado com orçamento real de tempo e memória.
3. Criar entrada, menu e fundos próprios do D3D, reutilizando os fluxos existentes de personagens, opções e navegação. Preservar o logo animado aprovado, acessibilidade, localização e os créditos das bases.
4. Manter a comparação com o renderizador original, o tempo da simulação, os saves e as colisões enquanto os novos elementos visuais são introduzidos.

Essas etapas se aplicam ao projeto inteiro. Tristram é a primeira região de validação; concluir sua apresentação não estabelece suporte 3D aos níveis procedurais.
