# Conferir modelos no Godot

O inspetor permite comparar cada arquivo 3D, suas referências documentadas e a conversão usada pelo jogo. A seleção é somente para leitura: não instala modelos, não altera a cena de Tristram e não promove candidatos a aceitos.

## Abrir a ferramenta

1. Clone este repositório e abra `editor/godot/project.godot` no Godot 4. A configuração atual do projeto usa Godot 4.7 e GL Compatibility.
2. Abra a cena [`editor/godot/review/model_inspector.tscn`](../editor/godot/review/model_inspector.tscn) e execute-a com **F6**.
3. Escolha um item na lista. Se o arquivo estiver ausente, use **Localizar GLB…** para associar sua cópia local. O SHA-256 precisa corresponder ao catálogo.
4. Use **Abrir catálogo local…** para revisar seu próprio pacote. O formato e o argumento opcional `-- --catalog=CAMINHO_DO_CATALOGO` estão no [README do addon](../editor/godot/addons/model_review/README.md).

O catálogo público contém metadados; a mídia pode aparecer como ausente. Modelos privados, referências nativas e resultados de geração permanecem locais até uma autorização separada de publicação. A ferramenta não baixa arquivos nem faz chamadas a serviços. Autoria, licença e direitos das fontes exigem verificação antes de distribuir mídia.

## Entender o catálogo

O levantamento inicial reúne **130 arquivos GLB com SHA-256 distintos**. Esse número inclui modelos, versões anteriores à redução de malha, derivações locais, candidatos, rigs e clipes de animação. Uma família pode ter vários arquivos e várias instâncias no mapa.

O pacote auditado tem **12 vínculos de mapa**, resolvidos para **9 GLBs fontes**. A cabana leste usa uma seleção separada, totalizando 10 GLBs fontes nos dois conjuntos. Gillian e Farnham reutilizam um modelo; taverna e ferraria têm vínculos separados para partes do mesmo objeto. **Aplicado** identifica o vínculo registrado no catálogo e não confirma a instalação no computador de quem abriu o inspetor.

Cada item conserva ID, revisão, etapa e SHA-256. Aprovação parcial, revisão técnica e aceitação integral são estados separados. Uma revisão visual positiva de uma fachada não encerra a avaliação do objeto em 360 graus, de interiores ou de outras instâncias.

## Comparar as evidências

| Evidência | O que mostra |
| --- | --- |
| Imagem enviada à geração | Arquivo documentado como entrada. A origem também é indicada: nativa, derivada ou gerada. |
| Referência nativa | Arte original usada como observação do objeto. Faces não visíveis continuam sem comprovação. |
| Vista inferida gerada | Complemento criado para orientar a reconstrução; detalhes inventados precisam de revisão. |
| Prévia do resultado | Imagem do resultado da tarefa ou de uma revisão local. Uma prévia da tarefa pode não corresponder exatamente ao arquivo pré-remesh. |
| GLB original | O arquivo da revisão selecionada, com sua geometria e seus materiais. |
| Modelo instalado | Os vértices, UVs e RGB embutido no D3D convertido e vinculado ao mapa. |

Na comparação 3D, **Mesma altura** aplica escala uniforme apenas à apresentação. **Unidades reais** mostra as escalas dos arquivos. Órbita, zoom e deslocamento acompanham os dois lados. Compare frente, costas, laterais, teto e base.

O GLB pode conservar mapas PBR e outros materiais que a conversão D3D não transporta. O D3D pode incorporar escala diferente em cada eixo, redução de textura e divisão em partes. Consulte esses dados na procedência do item antes de atribuir uma diferença à geração.

Esta prévia não reproduz a partida. Geometria residual nativa, iluminação, paleta e recortes ou interiores acrescentados pelo executável exigem comparação no jogo. Na cabana leste, parte do interior e das aberturas depende desse código. A comparação nativa com **Home** também não substitui uma captura com geometria 3D forçada.

## Registrar e contribuir

1. Confirme **ID, revisão e SHA-256** antes de avaliar. O mesmo SHA identifica os mesmos bytes; outro arquivo ou uma edição local exige outro SHA e uma revisão explícita. Não selecione pelo nome “mais recente” ou pela data do arquivo.
2. Em **Minha revisão**, registre o defeito ou a aprovação e seu escopo. Salve o JSON no local escolhido. A nota conserva a identidade avaliada e não muda o catálogo, a seleção do jogo ou o registro de aceitação.
3. Compartilhe a nota na issue do asset, com passos para reproduzir, câmera e instância afetada. Para fidelidade no jogo, acrescente comparação de geometria forçada na câmera nativa e vistas em 360 graus, respeitando os direitos das imagens. Remova caminhos pessoais e dados privados de qualquer material compartilhado.
4. Antes de corrigir ou criar um modelo, consulte as reservas em [`assets/registry.json`](../assets/registry.json) e no [catálogo de assets](ASSET-CATALOG.md). Use a issue existente ou peça a reserva ao mantenedor; variantes e componentes permanecem coordenados com sua família. O inspetor não cria outra fila de produção.

A mudança de revisão selecionada e a aceitação pertencem à revisão do projeto, seguindo [ASSET-APPROVAL.md](ASSET-APPROVAL.md) e [CONTRIBUTING.md](CONTRIBUTING.md). Conserve a versão anterior, a origem e a evidência que justificam cada alteração.
