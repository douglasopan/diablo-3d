# Inspetor comunitário de modelos

Abra `editor/godot/project.godot` no Godot 4.7 e execute a cena `review/model_inspector.tscn` com **F6**. A ferramenta não modifica a cena de Tristram, os modelos, o perfil do jogo nem o catálogo.

O catálogo público contém metadados. Modelos, referências nativas e resultados de serviços pagos não são distribuídos automaticamente. Selecione **Localizar GLB…** para associar sua cópia local: o inspetor verifica o SHA-256 antes de abrir. **Abrir catálogo local…** permite revisar um pacote local próprio. A aba **Minha revisão** salva uma nota JSON que você pode compartilhar; salvar a nota não publica arquivos nem promove um asset a aceito.

Também é possível executar a cena com `-- --catalog=/caminho/catalog.json`. Sem esse argumento, procura `res://local/model-review/inventory.json` e depois `res://review/catalog.json`. Caminhos relativos de um pacote são resolvidos a partir da pasta do catálogo; caminhos absolutos são aceitos somente no catálogo local escolhido. A ferramenta não faz chamadas à rede.

## Catálogo mínimo

```json
{
  "schemaVersion": 1,
  "models": [{
    "id": "modelo-exemplo",
    "name": "Objeto para revisão",
    "category": "props",
    "status": "candidate",
    "revision": "r1",
    "sha256": "SHA256_COMPLETO_DO_GLB",
    "glb": {"path": "models/objeto.glb"},
    "aliases": [],
    "references": [{"path": "images/referencia.png", "label": "Referência autoral", "kind": "api", "sourceKind": "generated", "role": "generation"}],
    "runtime": [],
    "warnings": []
  }]
}
```

`references.kind` distingue `api` (envio comprovado), `native` (arte nativa original), `generated` (vista gerada) e `unverified`. Não atribua origem ou envio sem evidência. `previews` contém imagens do resultado, separadas das referências.

Uma entrada opcional de `runtime` tem `path` para um arquivo D3DMESH1, `binding`, `group`, `origin: [x,0,z]`, `sha256` e `scale: [x,y,z]`. Partes com o mesmo `group` são visualizadas juntas, somando suas origens nativas antes de centralizar. Instâncias de grupos distintos ficam no seletor. O parser lê cabeçalho de 20 bytes, 60 bytes por triângulo e o RGB embutido; não reconstrói geometria por aproximação.

## Comparação

- **Mesma altura** aplica apenas escala uniforme de apresentação aos dois arquivos; as proporções de cada modelo são preservadas. **Unidades reais** remove esse ajuste.
- A primeira visão mostra o GLB original importado diretamente em memória, sem copiar o arquivo para o projeto. A segunda mostra os vértices e a textura RGB do D3D selecionado.
- **Cor sem iluminação** isola geometria e albedo. A opção PBR preserva o material original do GLB sob luz de inspeção; o D3D possui somente RGB e UV, sem mapas PBR.
- Órbita, zoom e deslocamento são sincronizados. Esta prévia não é uma captura da partida e não contém recortes, luzes ou interiores que o executável acrescenta.

Antes de compartilhar notas, imagens ou modelos, confira os direitos de distribuição e remova caminhos pessoais. Para a ferramenta e suas mudanças, vale a licença do repositório.
