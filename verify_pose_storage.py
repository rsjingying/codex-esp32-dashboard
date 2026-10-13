"""Compile the actual NVS adapter against a fault-injecting host backend."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent


class PoseStorageTests(unittest.TestCase):
    def test_reboot_validation_and_storage_failures(self):
        compiler = shutil.which('cc')
        self.assertIsNotNone(compiler, 'A C compiler is required for persistence checks')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / 'esp_err.h').write_text('''#pragma once
            typedef int esp_err_t;
            #define ESP_OK 0
            #define ESP_ERR_INVALID_STATE 1
            #define ESP_ERR_NVS_NOT_FOUND 2
            #define ESP_FAIL 3
            ''')
            (tmp / 'nvs.h').write_text('''#pragma once
            #include <stddef.h>
            #include "esp_err.h"
            typedef unsigned nvs_handle_t;
            #define NVS_READONLY 0
            #define NVS_READWRITE 1
            esp_err_t nvs_open(const char *, int, nvs_handle_t *);
            esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
            esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
            esp_err_t nvs_commit(nvs_handle_t);
            esp_err_t nvs_erase_key(nvs_handle_t, const char *);
            void nvs_close(nvs_handle_t);
            ''')
            (tmp / 'check.c').write_text(r'''
#include <assert.h>
#include <string.h>
#include "pose_storage.h"
#include "nvs.h"
static pose_settings_t disk, staged;
static bool exists, staged_exists;
static size_t disk_size=sizeof(disk);
static int open_failure, set_failure, commit_failure, erase_failure;
static unsigned opens, closes;
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h) {
    assert(!strcmp(name,"dashboard_pose")); (void)mode;
    if(open_failure) return ESP_FAIL;
    *h=1;opens++;staged=disk;staged_exists=exists;return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *data,size_t *size) {
    assert(h==1 && !strcmp(key,"settings"));
    if(!exists) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size>=disk_size);memcpy(data,&disk,disk_size);*size=disk_size;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size) {
    assert(h==1 && !strcmp(key,"settings") && size==sizeof(staged));
    if(set_failure) return ESP_FAIL;
    memcpy(&staged,data,size);staged_exists=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h==1);if(commit_failure) return ESP_FAIL;
    disk=staged;exists=staged_exists;return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {
    assert(h==1 && !strcmp(key,"settings"));
    if(erase_failure) return ESP_FAIL;
    if(!staged_exists) return ESP_ERR_NVS_NOT_FOUND;
    staged_exists=false;return ESP_OK;
}
void nvs_close(nvs_handle_t h) {assert(h==1);closes++;}
int main(void) {
    pose_tracker_t original, restored;pose_init(&original);pose_init(&restored);
    bool enabled=false;
    assert(pose_storage_load(&restored,&enabled)==ESP_ERR_NVS_NOT_FOUND);
    const pose_vector_t vectors[5]={{0,1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
    original.poses[0]=vectors[0];original.taught[0]=true;
    assert(pose_storage_save(&original,false)==ESP_OK);
    assert(pose_storage_load(&restored,&enabled)==ESP_OK && restored.taught[0] && !restored.taught[1]);
    assert(pose_storage_save(&original,true)==ESP_ERR_INVALID_STATE);
    for(unsigned i=0;i<5;i++) {original.poses[i]=vectors[i];original.taught[i]=true;}
    original.valid=true;original.still_samples=200;original.candidate=original.committed=1;
    assert(pose_storage_save(&original,true)==ESP_OK);
    pose_init(&restored);
    assert(pose_storage_load(&restored,&enabled)==ESP_OK && enabled);
    for(unsigned i=0;i<5;i++) assert(restored.taught[i] && pose_dot(restored.poses[i],vectors[i])==1);
    assert(!restored.valid && !restored.have_previous && restored.still_samples==0);
    assert(restored.candidate==-2 && restored.committed==-2);
    // A restored pose must still become stable; no event is emitted on the first sample.
    assert(pose_feed(&restored,1,0,0,100)==-1);
    unsigned events=0;
    for(unsigned tick=1;tick<50;tick++) if(pose_feed(&restored,1,0,0,100+tick*100)==1) events++;
    assert(events==1);
    assert(pose_storage_save(&original,false)==ESP_OK);
    assert(pose_storage_load(&restored,&enabled)==ESP_OK && !enabled);
    pose_settings_t good=disk;
    open_failure=1;assert(pose_storage_save(&original,true)==ESP_FAIL);open_failure=0;
    set_failure=1;assert(pose_storage_save(&original,true)==ESP_FAIL);set_failure=0;
    commit_failure=1;assert(pose_storage_save(&original,true)==ESP_FAIL);commit_failure=0;
    assert(!memcmp(&disk,&good,sizeof(good)));
    // Invalid records must not modify the caller's current calibration or flag.
    pose_tracker_t before=restored;
    disk.version=2;assert(pose_storage_load(&restored,&enabled)==ESP_ERR_INVALID_STATE);
    assert(!memcmp(&before,&restored,sizeof(before)) && !enabled);disk=good;
    disk_size=sizeof(disk)-1;assert(pose_storage_load(&restored,&enabled)==ESP_ERR_INVALID_STATE);disk_size=sizeof(disk);
    disk.poses[0].x=NAN;assert(pose_storage_load(&restored,&enabled)==ESP_ERR_INVALID_STATE);disk=good;
    disk.poses[1]=disk.poses[0];assert(pose_storage_load(&restored,&enabled)==ESP_ERR_INVALID_STATE);disk=good;
    disk.mask=32;assert(pose_storage_load(&restored,&enabled)==ESP_ERR_INVALID_STATE);disk=good;
    disk.mask=1;disk.enabled=1;assert(pose_storage_load(&restored,&enabled)==ESP_ERR_INVALID_STATE);disk=good;
    erase_failure=1;assert(pose_storage_clear()==ESP_FAIL && exists);erase_failure=0;
    commit_failure=1;assert(pose_storage_clear()==ESP_FAIL && exists);commit_failure=0;
    assert(pose_storage_clear()==ESP_OK);
    assert(pose_storage_load(&restored,&enabled)==ESP_ERR_NVS_NOT_FOUND);
    assert(pose_storage_clear()==ESP_OK);assert(opens==closes);
}
''')
            executable = tmp / 'check'
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(tmp), '-I', str(ROOT / 'firmware'),
                            str(ROOT / 'firmware/pose_storage.c'), str(tmp / 'check.c'),
                            '-lm', '-o', str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
