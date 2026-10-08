# Aplicação do lote de arquitetura — 8 de outubro de 2026

O pacote do restante de Tristram usa **nove construções novas e um poço em 12 vínculos nativos**, totalizando 75.069 triângulos. O poço foi acrescentado após a primeira aplicação de 11 vínculos. A cabana leste selecionada, sua iluminação e o executável instalado são preservados. Os modelos são candidatos para revisão, não assets com aceitação artística integral.

| Construção | Instância no mapa |
| --- | --- |
| Casa de Gillian | `house-gillian` |
| Casa de Pepin | `house-pepin` |
| Casa de Adria | `house-adria` |
| Casa norte | `house-north` |
| Casa de Farnham | `house-southeast` |
| Cabana oeste | `cabin-west` |
| Catedral | `cathedral` |
| Taverna | `tavern-main`, `tavern-wing` |
| Ferraria | `smithy-house`, `smithy-forge` |
| Poço limpo | `well`, variante `clean-water` |

A taverna e a ferraria conservam seus modelos inteiros no editor. Na conversão, cada conjunto de triângulos é particionado entre seus dois vínculos nativos pela distância aos limites originais: não há faces duplicadas, descartadas ou novas. Os dois arquivos compartilham a mesma imagem de textura e preservam UVs e orientação das faces. Isso permite usar a ponte estática existente, sem alterar a simulação ou recompilar o jogo.

O pacote local utiliza nomes com a identidade do modelo e um prefixo de SHA-256, evitando substituir silenciosamente uma revisão anterior. O manifesto `d3d-maps/tristram.ini` seleciona explicitamente os arquivos e hashes. Pacotes fontes, masters GLB, comprovantes da conversão e evidências permanecem privados. A aplicação é feita no perfil habitual; a entrada para jogar continua **Iniciar-Tristram.cmd**.

## Evidência e limites

- O carregador C++ real confirmou os 12 vínculos, arquivos e hashes no cenário de diagnóstico com poço limpo. Os 11 arquivos anteriores mantiveram exatamente seus caminhos e hashes. A exportação preservou mapa, colisão, atores e RNG nativos, além da cabana leste com suas fontes de fogo.
- Antes do acréscimo do poço, o teste finito de aberturas com 11 vínculos passou **707 verificações**, renderizando o cenário carregado na CPU e na GPU física, sem WARP ou fallback, preservando a cabana, luz e estado nativo. Suas capturas são de um diagnóstico com foco na região leste; não cobrem visualmente todos os prédios e não são screenshots de uma janela de partida.
- O benchmark amplo de GPU parou antes de desenhar porque sua fixture ainda exige cinco entradas no menu antigo. Esse resultado não é uma medição de desempenho do novo pacote; a fixture precisa acompanhar o menu na próxima integração.
- Posições, escala, orientação, materiais e fidelidade ao original ainda precisam de revisão por objeto na câmera original e em 360°. O ajuste inicial usa os limites dos substitutos procedurais anteriores; isso não comprova coincidência com a arte original. Após testar, o usuário considerou a maioria das construções insatisfatória. A catedral teve seu exterior aprovado, com pedido de subir sua posição e remover fragmentos da reconstrução anterior. Preservar esse modelo, sem regenerá-lo nem alterar seu material como consequência da rejeição de outras casas.
- Os prédios novos ainda não têm o conjunto de recortes, interiores e fontes de fogo desenvolvido para a cabana leste. Sombras pintadas restantes no chão continuam pendentes.
- A entrada aberta das catacumbas ficou fora: a variante não corresponde à entrada fechada do mapa validado. Não alterar progressão ou colisão para forçar essa seleção.
- O poço usa 2.992 triângulos, derivados localmente do master preservado. Sua seleção vale somente para água limpa; quando a missão exige `poisoned-water`, o vínculo não corresponde e o jogo conserva o poço nativo. Nenhum estado de missão é alterado para forçar o modelo.
- O acréscimo do poço foi preparado com a partida aberta: somente seu novo arquivo imutável, o recibo e o manifesto foram gravados, com o manifesto substituído atomicamente por último. O executável, os 11 modelos anteriores, a cabana e a luz não mudaram; configurações e saves não foram escritos pela transação. A cena já carregada permanece em cache, portanto o novo pacote depende da próxima carga do cenário.
- Árvores, rochas, props e personagens gerados não fazem parte deste pacote. Sua presença na cena Godot não comprova aplicação no jogo.

A revisão usa o projeto `editor/godot/project.godot`, com a cena privada `local/tristram-production/tristram-review.tscn`. Essa cena contém candidatos adicionais e placeholders: não representa exatamente o conjunto já aplicado no perfil. A lista acima e o recibo local de aplicação identificam o pacote do jogo.

## Revisão dos arquivos e da conversão

O [inspetor comunitário Godot](MODEL-INSPECTOR.md) separa o GLB de origem, as imagens comprovadamente enviadas à geração e o D3D efetivamente selecionado. O catálogo público registra identidades e transformações; a mídia permanece separada. São 130 arquivos GLB únicos entre masters, versões e clipes, não 130 objetos distintos. Os 12 vínculos do pacote correspondem a nove GLBs únicos; a cabana leste preservada acrescenta uma fonte e um D3D.

A auditoria dos 11 vínculos de construções confirmou arquivos, contagens, UVs e posições derivadas da transformação registrada, sem evidência de troca de modelo ou aplicação dupla da escala. Entretanto, a conversão ajustou eixos independentemente para caber nos limites antigos, deformando proporções de vários objetos. A casa norte é uma exceção comprovada, com escala uniforme. Taverna e ferraria continuam com partições disjuntas do mesmo master.

O formato atual também reduz albedo de 4096² para 2048² e não conserva os normal maps, mapas PBR nem as normais suaves dos GLBs auditados. O jogo acrescenta sua iluminação e quantização de paleta; a prévia do inspetor não simula essa etapa. Essas perdas podem contribuir para a aparência diferente, mas não substituem uma comparação pareada na partida para determinar cada causa.

Novas aplicações e regenerações ficam suspensas enquanto as referências e os objetos são revisados individualmente. Uma nota de colaborador não promove automaticamente um asset. A limpeza de 14 células da torre antiga da Catedral foi corrigida no código candidato, mantendo as duas células de entrada nativas; sua instalação e o ajuste de altura têm evidências próprias e não fazem parte da aplicação histórica acima.
