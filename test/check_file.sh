#!/bin/bash
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

expected_lines=$2
echo "get_file_content $1  | wc -l"
for file in $(ls $files); do
  echo file:$file
  num_posix_lines=$(get_file_content "$file" | wc -l 2> /dev/null)
done
echo $num_posix_lines
if [[ "$num_posix_lines" -ne "$expected_lines" ]]; then
  for file in $(ls $files); do
    echo file:$file
    get_file_content "$file" 
  done
  rm -r $1 2> /dev/null
  exit 1
else
  for file in $(ls $files); do
  echo file:$file
    if jq --slurp -e >/dev/null 2>&1 <<< `get_file_content "$file" | grep -v "Binary"  | awk '{$1=$1;print}'`; then
      echo "Parsed JSON successfully and got something other than false/null";
    else
      echo "Failed to parse JSON, or got false/null";
      jq --slurp -e <<< `get_file_content "$file" | grep -v "Binary"  | awk '{$1=$1;print}'`
      get_file_content "$file"  | awk '{$1=$1;print}'
      exit 1
    fi
  done
fi
rm -r $1 2> /dev/null