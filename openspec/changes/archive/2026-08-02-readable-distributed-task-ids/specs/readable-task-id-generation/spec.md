## ADDED Requirements

### Requirement: Generate task IDs from random words and suffix

The system SHALL provide a free function `GenerateTaskId() -> std::string` that returns a string in the format `adjective_noun_verb_xxxxxxxxxx`, where each word is randomly selected from a curated list and the suffix is 8 random lowercase alphanumeric characters.

#### Scenario: Generate a valid task ID
- **WHEN** `GenerateTaskId()` is called
- **THEN** it SHALL return a string matching the format `<adjective>_<noun>_<verb>_<8 lowercase alphanumeric chars>`

#### Scenario: Words and suffix are randomly selected
- **WHEN** `GenerateTaskId()` is called multiple times
- **THEN** the returned strings SHALL differ with high probability (collision probability < 10⁻¹⁰ for 10 million calls)

#### Scenario: Suffix contains only lowercase letters and digits
- **WHEN** `GenerateTaskId()` is called
- **THEN** the 8-character suffix portion SHALL contain only characters from `a-z` and `0-9`, in lowercase

### Requirement: Word lists are curated English words

The system SHALL use curated word lists containing approximately 200 adjectives, 500 nouns, and 200 verbs. Words SHALL be simple, pronounceable English words without offensive connotations.

#### Scenario: Adjective list provides variety
- **WHEN** words are selected from the adjective list
- **THEN** there SHALL be at least 200 distinct adjectives to choose from

#### Scenario: Noun list provides variety
- **WHEN** words are selected from the noun list
- **THEN** there SHALL be at least 500 distinct nouns to choose from

#### Scenario: Verb list provides variety
- **WHEN** words are selected from the verb list
- **THEN** there SHALL be at least 200 distinct verbs to choose from

### Requirement: Thread-local random state

The system SHALL use thread-local random state (e.g., thread-local `std::mt19937_64`) seeded from `std::random_device` to generate IDs. Each thread calling `GenerateTaskId()` SHALL have its own independent random state.

#### Scenario: Multiple threads generate unique IDs concurrently
- **WHEN** multiple threads call `GenerateTaskId()` concurrently
- **THEN** each thread SHALL have its own independent random state
- **AND** threads SHALL not contend for shared locks

#### Scenario: Random state is seeded securely
- **WHEN** a thread first calls `GenerateTaskId()`
- **THEN** the thread-local random state SHALL be seeded from `std::random_device`

### Requirement: ID length and structure

The generated task ID SHALL be approximately 20-30 characters long, consisting of three words separated by underscores followed by an 8-character alphanumeric suffix.

#### Scenario: ID structure is readable and compact
- **WHEN** `GenerateTaskId()` is called
- **THEN** the returned string SHALL have the format `word_word_word_xxxxxxxxxx`
- **AND** the total length SHALL be approximately 20-30 characters
- **AND** all words and the suffix SHALL be lowercase
