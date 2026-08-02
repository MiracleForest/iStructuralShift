# iStructuralShift

A LeviLamina server mod for managing Minecraft structure templates.

## Description

`iStructuralShift` lets you export, import, list, and remove structure templates between the world's internal structure storage and `.mcstructure` files.

When exporting, the mod prefers the in-memory structure cache, so recently loaded or modified structures can be exported directly.  
When importing or removing structures, the mod also clears the corresponding in-memory cache to keep runtime state consistent with storage.

## Features

- Export structure templates from the world to `.mcstructure` files
- Import `.mcstructure` files back into the world
- List all available structures from both storage and memory cache
- Remove one or all structures from storage and memory cache
- Refresh command soft enums after structure changes
- Optionally bypass the vanilla structure size range limit
- Built-in i18n resources for `en-us`, `zh-cn`, and `zh-tw`

## Usage

Command alias: `/iss`

### Commands

```text
/structural_shift update_enum
/structural_shift list
/structural_shift export <name>
/structural_shift export_all
/structural_shift import <name>
/structural_shift import_all
/structural_shift remove <name>
/structural_shift remove_all
/structural_shift bypass_limit [true|false]
```

### Command Notes

- `export <name>` exports a single structure to `plugins/iStructuralShift/data/structures/<name>.mcstructure`
- `export_all` exports all known structures, including structures currently only present in memory
- `import <name>` imports `plugins/iStructuralShift/data/structures/<name>.mcstructure` into world storage and clears the runtime cache for that structure
- `import_all` imports every `.mcstructure` file in the structures directory
- `remove <name>` removes the structure from both world storage and the in-memory cache
- `remove_all` removes every known structure from both world storage and the in-memory cache
- `bypass_limit` turns the vanilla structure command range limit on or off

## File Location

Exported and imported structure files are stored in:

```text
plugins/iStructuralShift/data/structures/
```

## Installation

> [!CAUTION]
> This installation guide is for reference only. Please follow your actual server environment and package source.

### Server

#### Using Lip (Recommended)

1. Navigate to your Minecraft server directory
2. Execute the following command:
   ```bash
   lip install github.com/MiracleForest/iStructuralShift#server
   ```

#### Manual Installation

1. Download the latest release package from the [Releases page](https://github.com/MiracleForest/iStructuralShift/releases)
2. Extract the package into your server's `./plugins` directory

## Requirements

- LeviLamina
- A compatible Bedrock Dedicated Server environment
