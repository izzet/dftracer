#!/bin/bash
expected_lines=$2
files=$1

get_file_content() {
  local file="$1"
  if [[ "${file##*.}" == "pfw" ]]; then
    grep cat "$file"
  elif [[ "${file##*.}" == "gz" || "${file##*.}" == "pfw.gz" ]]; then
    zgrep cat "$file"
  else
    grep cat "$file"
  fi
}

num_posix_lines=0
for file in $(ls $files); do
  num_posix_lines_local=$(get_file_content "$file" | wc -l 2> /dev/null)
  num_posix_lines=$((num_posix_lines + num_posix_lines_local))
done

if [[ "$num_posix_lines" -lt "$expected_lines" ]]; then
  echo "Found $num_posix_lines expected $expected_lines"
  for file in $(ls $files); do
    get_file_content "$file" 
  done
  exit 1
else
  for file in $(ls $files); do
    if jq --slurp -e >/dev/null 2>&1 <<< `get_file_content "$file" | grep -v "Binary" | awk '{$1=$1;print}'`; then
      echo "Parsed JSON successfully and got something other than false/null";
    else
      echo "Failed to parse JSON, or got false/null";
      jq --slurp -e <<< `get_file_content "$file" | grep -v "Binary" | awk '{$1=$1;print}'`
      get_file_content "$file"  | awk '{$1=$1;print}'
      exit 1
    fi
  done
  
fi
rm -r $1 2> /dev/null
exit 0