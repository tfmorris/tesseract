
# Delete if exists to cover case where no output is produced
if [ -f phototest-test.txt ]
then
    rm phototest-test.txt
fi

tesseract phototest.tif phototest-test

# Compare to our ground truth
diff phototest-gt.txt phototest-test.txt

