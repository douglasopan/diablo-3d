# Conferência do estudo visual

Verificação concluída em **8 de outubro de 2026**. O objeto verificado é a galeria de propostas em `docs/hud-study/`, sem executar o jogo.

## Cobertura e arquivos

- **223 exemplos, 223 IDs únicos e 13 famílias**, com uma imagem JPEG de 1920×1080 para cada ID. Nenhuma prévia ausente, extra ou vazia.
- A contagem inclui variantes e estados; não representa 223 menus independentes. As condições e fontes estão no [índice de cobertura](COVERAGE.md).
- **732 links locais** do README e do índice conferidos, sem referências quebradas. Proveniência e este relatório foram adicionados ao final da conferência.
- As cópias das seis imagens geradas correspondem exatamente aos arquivos de origem. O logo autoral corresponde à fonte do repositório. As referências aprovadas `01-hud-16x9-v2.png` e `05-fundo-menus.png` estão preservadas. Tamanhos, dimensões e hashes constam em [provenance.json](provenance.json).

## Galeria e formatos

- Os **223 exemplos em 1920×1080** foram renderizados e tiveram os limites dos painéis medidos no navegador: nenhum painel permanece fora do viewport. Uma medição anterior ao primeiro ajuste de layout foi repetida e resolvida.
- **22 verificações representativas adicionais**: 16 em 1280×720 e seis em 2560×1080. Nenhum limite de painel fora do viewport. Foram salvas mais duas imagens das abas compactas de personagem/inventário, totalizando 24 arquivos em `responsive/`.
- As dimensões efetivas dos arquivos responsivos exportados são 1280×720 (18 imagens) e 2496×1080 (seis imagens ultrawide); o canvas de referência ultrawide é 2560×1080. O manifesto público registra as dimensões efetivas.
- A inspeção visual incluiu menu principal, configurações de vídeo, grimório, loja em grade e as duas abas compactas de personagem/inventário. Listas extensas usam rolagem interna; os exemplos não prometem exibir todo o conteúdo simultaneamente.
- Sintaxe de `study.js` e `catalog-data.js` conferida. Não foram observados erros ou avisos no console durante a validação. Os dados embutidos eliminam a dependência de requisições para abrir o catálogo localmente.
- Busca restrita aos arquivos do estudo não encontrou os padrões de credenciais e chaves privadas procurados.

As medições estão em [data/layout-checks.json](data/layout-checks.json). Os exemplos são composições ilustrativas com texto e layout determinísticos; as imagens exportadas são capturas da galeria.

## Limites da verificação

Esta entrega **não valida os fluxos no executável**, disponibilidade do multiplayer no build local, desempenho do compositor, navegação completa por controle, leitores de tela, localização ou todos os contrastes e alvos clicáveis. Ícones e retratos esquemáticos precisam de arte de produção. A revisão visual foi representativa; a medição de limites foi feita para todos os exemplos desktop.

O trabalho deste estudo permanece em `docs/hud-study/`, sem preparar commit ou alterar arquivos da integração em andamento no outro chat.
