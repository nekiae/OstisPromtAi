# Раздел: Помощник по генерации промтов (intent=generate_prompt)

## Назначение
Мини‑раздел БЗ для диалоговой логики генерации промтов. Ассистент принимает сообщение с интентом `generate_prompt`, собирает слоты `ROLE`/`DOMAIN`/`TASK` (с добором недостающих) и выдает подтверждающую фразу.

- **Интент**: `generate_prompt` (`nrel_wit_ai_idtf`)
- **Слоты**: `nrel_role`, `nrel_domain`, `nrel_task`
- **Язык фраз**: русский (`lang_ru`)
- **Приоритет добора**: TASK → DOMAIN → ROLE

## Структура
- `section_prompt_assistant.scs` — объявление раздела.
- `concepts/` — классы: `concept_generate_prompt_message`, `concept_role`, `concept_domain`, `concept_task`; инстансы базовых ролей/доменов.
- `relations/` — отношения‑слоты: `nrel_role`, `nrel_domain`, `nrel_task`.
- `phrases/` — банк фраз и шаблон полного ответа с плейсхолдерами `${_role_name}`, `${_domain_name}`, `${_task_text}` (см. `phrase_full_ru.scs`).
- `logic_rules/` — правила GWF: классификация, полный ответ, запрос недостающего слота.
- `tests/` — минимальные сценки для ручной проверки.

## Как это работает
1. Входящее сообщение `_m` помечается `=> nrel_wit_ai_idtf: [generate_prompt];`.
2. (Опционально) Внешний адаптер добавляет слоты:
   - `_m -> nrel_role: role_*;`
   - `_m -> nrel_domain: domain_*;`
   - `_m -> nrel_task: _task_node;` где `_task_node` — узел с текстовой ссылкой `=> nrel_main_idtf: [<текст задачи>] (* <- lang_ru;; *);`.
3. Запускаются правила `logic_rules/`:
   - `lr_generate_prompt_classification.gwf` — относит `_m` к `concept_generate_prompt_message` по `nrel_wit_ai_idtf`.
   - Если есть все три слота — `lr_generate_prompt_full.gwf` назначает шаблон `concept_generate_prompt_phrase_full` через `nrel_answer_pattern`.
   - Если нет `nrel_task` — `lr_generate_prompt_need_task.gwf` назначает `concept_clarify_task_phrase`.
   - Если нет `nrel_domain` — `lr_generate_prompt_need_domain.gwf` назначает `concept_clarify_domain_phrase`.
   - Если нет `nrel_role` — `lr_generate_prompt_need_role.gwf` назначает `concept_clarify_role_phrase`.
4. Подстановка плейсхолдеров происходит по шаблону `@pattern_full` в `phrases/phrase_full_ru.scs`:
   - `${_role_name}` ← главный идентификатор роли
   - `${_domain_name}` ← главный идентификатор домена
   - `${_task_text}` ← текст из узла задачи

## Тестирование (вручную)
Откройте файлы в `tests/` и последовательно активируйте правила:
- `tests/test_dialog_happy_path.scs` — заданы все три слота; ожидается класс ответа `concept_generate_prompt_phrase_full` и текст с «Врач», «Медицина», «составить план лекции».
- `tests/test_dialog_missing_task.scs` — нет `nrel_task`; ожидается `concept_clarify_task_phrase`.
- `tests/test_dialog_missing_domain.scs` — нет `nrel_domain`; ожидается `concept_clarify_domain_phrase`.
- `tests/test_dialog_missing_role.scs` — нет `nrel_role`; ожидается `concept_clarify_role_phrase`.

## Расширяемость (идея)
- Новые сущности: `TONE`, `OUTPUT_FORMAT` → добавить `nrel_tone`, `nrel_output_format`, новые фразы и правила.
- Мультиязычность: продублировать банк фраз `lang_en` и выбрать по профилю пользователя.
- Нормализация синонимов: добавить инстансы ролей/доменов и сопоставление в адаптере.
