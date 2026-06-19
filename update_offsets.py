import argparse
import csv
import struct
from pathlib import Path


class PatchOffsetUpdater:
    def __init__(self, tsv_path, game_dir):
        self.tsv_path = Path(tsv_path)
        self.game_dir = Path(game_dir)
        self.bin_cache = {}

    def _get_bin_index(self, bin_name):
        if bin_name in self.bin_cache:
            return self.bin_cache[bin_name]

        bin_path = self.game_dir / f"{bin_name}.bin"
        if not bin_path.exists():
            return None

        index = {}
        try:
            with open(bin_path, "rb") as f:
                header = f.read(8)
                if len(header) < 8:
                    return None

                file_count, name_table_size = struct.unpack("<II", header)

                entries = []
                for _ in range(file_count):
                    entries.append(struct.unpack("<III", f.read(12)))

                names_data = f.read(name_table_size)

                for name_off, abs_off, size in entries:
                    end = names_data.find(b"\x00", name_off)
                    res_name = names_data[name_off:end].decode(
                        "shift-jis", errors="ignore"
                    )
                    index[res_name] = (abs_off, size)

            self.bin_cache[bin_name] = index
            return index
        except Exception as e:
            print(f"Error indexing {bin_path.name}: {e}")
            return None

    def _read_hzc_offset(self, bin_name, res_name):
        index = self._get_bin_index(bin_name)
        if not index or res_name not in index:
            return None

        abs_off, size = index[res_name]
        bin_path = self.game_dir / f"{bin_name}.bin"

        try:
            with open(bin_path, "rb") as f:
                f.seek(abs_off)
                head = f.read(min(size, 44))
                if head[:4] == b"hzc1":
                    ox, oy = struct.unpack("<HH", head[24:28])
                    return ox, oy
        except Exception as e:
            print(f"Error reading {res_name} from {bin_name}: {e}")
        return None

    def run(self):
        if not self.tsv_path.exists():
            print(f"TSV file not found: {self.tsv_path}")
            return

        with open(self.tsv_path, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f, delimiter="\t")
            headers = reader.fieldnames
            rows = list(reader)

        updated_count = 0
        for row in rows:
            orig_path = row.get("originalName", "")
            if "/" not in orig_path:
                continue

            parts = orig_path.split("/")
            bin_name = parts[0]
            res_name = parts[1]

            offset = self._read_hzc_offset(bin_name, res_name)
            if offset is not None:
                row["offsetX"], row["offsetY"] = str(offset[0]), str(offset[1])
                updated_count += 1
                print(f"{orig_path} -> ({offset[0]}, {offset[1]})")

        with open(self.tsv_path, "w", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=headers, delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)

        print(f"\nSuccess! Updated {updated_count} offsets in {self.tsv_path.name}")


def main():
    parser = argparse.ArgumentParser(description="Update patch TSV offsets")
    parser.add_argument("-t", "--tsv", required=True, help="TSV file path")
    parser.add_argument(
        "-d", "--dir", required=True, help="Game directory (containing .bin files)"
    )

    args = parser.parse_args()

    updater = PatchOffsetUpdater(args.tsv, args.dir)
    updater.run()


if __name__ == "__main__":
    main()
