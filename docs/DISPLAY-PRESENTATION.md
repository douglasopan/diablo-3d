# Apresentação do Diablo 3D: resolução, nitidez e interface

O D3D pretende transformar todo o Diablo 1 em 3D, incluindo os níveis procedurais. A resolução e a apresentação já podem ser comparadas usando os recursos do DevilutionX. A nova entrada, o novo menu e a separação completa entre escala da interface e renderização do mundo continuam como etapas próprias de desenvolvimento.

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

O modelo Meshy não acompanha o repositório público. Sem esse arquivo local, omita `-MeshyReview` para comparar a reconstrução disponível. O iniciador normal continua usando o perfil habitual.

| Opção | Área interna | Ampliação | O que comparar |
|---|---|---|---|
| Nítido | 960×540 | Sem suavização (`Scaling Quality=0`) | Pixels definidos; serrilhado pode ficar mais visível em escalas fracionárias. |
| Suave | 960×540 | Filtro de qualidade 2 do SDL | Suavização na saída, conforme o renderizador de apresentação disponível. |
| Campo ampliado | 1280×720 | Mesmo filtro de Suave | Mais cenário; objetos e interface ficam menores na mesma tela. O trabalho do renderizador pode aumentar. |

Suave reproduz o filtro que já estava configurado no perfil local analisado. Nítido permite comparar outro método de ampliação; Campo ampliado muda a área de jogo. Nenhuma opção importa código ou arte do Belzebub.

Cada opção cria um `perfil-apresentacao-*` separado, com uma cópia inicial da configuração e dos saves locais de personagem. O progresso dessa cópia fica separado. Selecionar a opção novamente reaplica suas chaves gráficas; as demais opções do perfil são preservadas. Com `-MeshyReview`, a cópia inicial vem do perfil de revisão quando ele existe, e o modelo local é instalado nessa cópia.

O iniciador informa se faltam dados ou o modelo solicitado. `-PrepareOnly` prepara o perfil sem iniciar o jogo. Os perfis de comparação são ignorados pelo Git, assim como saves, modelos e extrações privadas.

F4 alterna a vista em Tristram; o botão do meio gira/inclina a câmera, a roda ajusta o zoom e Home restaura a vista original. Nas outras regiões, o renderizador original permanece ativo. Compare o mesmo objeto, estado e câmera ao avaliar nitidez; um campo de visão maior não é uma comparação de detalhe equivalente.

## O que foi aprendido com o Belzebub

O [registro das bases e créditos](ENGINE-BASES-AND-CREDITS.md) reúne as fontes oficiais e distingue o código realmente usado das referências estudadas. A [auditoria dos recursos](REFERENCE-SOURCES.md) registra a arte encontrada e seus limites.

A instalação fornecida contém SDL2, OpenGL/GLEW e recursos de interface próprios. A [engenharia reversa do executável](BELZEBUB-BINARY-ANALYSIS.md) examina as referências e os argumentos das chamadas gráficas, além das strings: confirma contexto OpenGL, alocação de texturas, filtragem linear, composição por alfa e um caminho de brilho. O estudo estático registra os limites de cada achado. A ferramenta de controle de aplicativos falhou antes de abrir o mod; não houve observação da sua tela em execução nesta etapa.

O atlas local `ctrlpan/modernui.png`, de 1024×1024, contém globos, barra de ações, botões e painéis de atributos. É evidência de arte adicional de interface. Sua dimensão é a de um atlas composto, não a resolução de uma casa ou personagem. Nenhuma imagem desse atlas foi incorporada ao D3D ou publicada como asset.

## Limites e próxima evolução

O nosso renderizador 3D atual desenha geometria na CPU e converte o resultado para a paleta do jogo. A apresentação SDL amplia a imagem final usando os recursos disponíveis do sistema. Aumentar somente `Width` e `Height` aumenta a área visível: a projeção nativa mantém sua escala em pixels. Isso não faz o mesmo objeto ganhar automaticamente mais amostras na tela, e os menus herdados não têm um controle geral de escala independente.

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
