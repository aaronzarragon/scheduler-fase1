# Cómo trabajar en este repo (Gitflow)

## Ramas

| Rama | Para qué sirve | ¿Se hace push directo? |
|---|---|---|
| `main` | Versión entregable y estable. Solo recibe merges desde `develop` o `hotfix/*`. | No, solo con Pull Request |
| `develop` | Integración: aquí se junta el trabajo de todos. | No, solo con Pull Request |
| `feature/<nombre>` | Una funcionalidad o tarea (ej: `feature/cmd-gantt`). | Sí |
| `hotfix/<nombre>` | Arreglo urgente sobre `main`. | Sí |

## Primera vez

```bash
git clone https://github.com/aaronzarragon/scheduler-fase1.git
cd scheduler-fase1
git checkout develop
```

## Flujo para cada tarea

```bash
# 1. Actualiza develop
git checkout develop
git pull

# 2. Crea tu rama
git checkout -b feature/mi-tarea

# 3. Trabaja y haz commits pequeños
git add .
git commit -m "feat: agrega comando stats"

# 4. Sube tu rama
git push -u origin feature/mi-tarea
```

5. En GitHub, abre un **Pull Request** de `feature/mi-tarea` hacia `develop`.
6. Cuando se apruebe, haz el merge y borra la rama.

## Mensajes de commit

Usa un prefijo: `feat:` (funcionalidad), `fix:` (arreglo), `docs:` (documentación), `refactor:`, `test:`.

## Compilar (WSL)

```bash
cd scheduler
make
./scheduler
```
