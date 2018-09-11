<?php

function user_migrate_import_form($form, &$form_state) {
  $form['user_import_file'] = array(
    '#type' => 'managed_file',
    '#title' => t('Upload file'),
    '#required' => TRUE,
    '#description' => t('allowed extensions: json'),
    '#upload_location' => 'private://migrate_users/imports',
    '#upload_validators' => array('file_validate_extensions' => array('json')),
  );
  $form['user_import_submit'] = array(
    '#type' => 'submit',
    '#value' => t('Import'),
    '#submit' => array('user_import_submit'),
  );
  return $form;
}

/**
 * Availability Import Batch.
 */
function user_import_submit(&$form, &$form_state) {
  global $user;
  $file = file_load($form_state['values']['user_import_file']);
  $file->status = FILE_STATUS_PERMANENT;
  file_save($file);
  file_usage_add($file, 'user_migrate', 'import_users', $user->uid);
  $ext = pathinfo($file->uri, PATHINFO_EXTENSION);
  if ($ext != "json") {
    form_set_error('', 'Upload a JSON file.');
  }
  else {
    $filename = $file->filename;
    $content = file_get_contents($file->uri);
    $users = json_decode($content, TRUE);
    if($users === null) {
      // logging activity
      $message = t('Invalid JSON.');
      write_log($message, 'import_logs');
    }
    else {
      // Batch to perform add/update availability data.
      $batch = process_user_import_batch($users['users']);
      batch_set($batch);
    }
  }
}

/********************* Import batch *********************/

/**
 * Batch foreach function.
 */
function process_user_import_batch($data) {
  $operations = array();
  foreach ($data as $record) {
    $operations[] = array('process_user_import_batch_op', array($record));
  }
  $batch = array(
    'operations' => $operations,
    'finished' => 'process_user_import_batch_op_finished',
    'file' => drupal_get_path('module', 'user_migrate') . '/includes/user_migrate.import.inc',
  );
  return $batch;
}

/**
 * This is the function that is called on each operation in batch 1.
 */
function process_user_import_batch_op($record, &$context) {
  $record = json_decode($record, TRUE);
  if($record === null) {
    // logging activity
    $message = t('Invalid JSON for user.');
    write_log($message, 'import_logs');
    $context['finished'] = 1;
  }

  watchdog('user_migrate_data_record', '<pre>' . print_r($record, TRUE) . '</pre>');
  $context['message'] = t('Importing user: :name (:email)', array(':name' => $record['core_details']['name'], ':email' => $record['core_details']['mail']));

  $data = db_select('users', 'users')
    ->fields('users', array('uid'))
    ->orderBy('uid', 'DESC')
    ->range(0,1)
    ->execute()->fetchAll();

  $uid = (int) $data[0]->uid;
  $user = user_load_by_mail($record['core_details']['mail']);
  if(empty($user)) {
    $uid = $uid+1;
    db_insert('users')
      ->fields(array(
        'uid' => $uid,
        'name' => $record['core_details']['name'],
        'pass' => $record['core_details']['pass'],
        'mail' => $record['core_details']['mail'],
        'theme' => $record['core_details']['theme'],
        'signature' => $record['core_details']['signature'],
        'signature_format' => $record['core_details']['signature_format'],
        'created' => $record['core_details']['created'], // time()
        'access' => $record['core_details']['access'], // 0
        'login' => $record['core_details']['login'],
        'status' => $record['core_details']['status'],
        'timezone' => $record['core_details']['timezone'],
        'language' => $record['core_details']['language'],
        'picture' => $record['core_details']['picture'],
        'init' => $record['core_details']['init'],
        'data' => $record['core_details']['data'],
      ))->execute();

    // logging activity
    $message = t('New user imported: @name(@email - @uid) [SUCCESS]', array("@name" => $record['core_details']['name'], '@email' => $record['core_details']['mail'], '@uid' => $uid));
    write_log($message, 'import_logs');
  }
  else {
    $uid = $user->uid;
    // logging activity
    $message = t('Existing user: @name(@email - @uid)', array("@name" => $user->name, '@email' => $user->mail, '@uid' => $uid));
    write_log($message, 'import_logs');
  }

  $user = user_load($uid);
  if(isset($record['fields'])) {
    if(isset($record['fields']['field_user_salutation'])) {
      // Remove the existing data.
      unset($user->field_user_salutation);
      $user->field_user_salutation['und'][0]['value'] = $record['fields']['field_user_salutation'];

      $message = t(' -- Updating user salutation [SUCCESS]');
      write_log($message, 'import_logs');
    }

    if(isset($record['fields']['field_full_name'])) {
      // Remove the existing data.
      unset($user->field_full_name);
      $user->field_full_name['und'][0]['value'] = $record['fields']['field_full_name'];

      $message = t(' -- Updating user full name [SUCCESS]');
      write_log($message, 'import_logs');
    }

    if(isset($record['fields']['field_institution'])) {
      // Remove the existing data.
      unset($user->field_institution);
      $i = 0;
      foreach ($record['fields']['field_institution'] as $institution_name) {
        $institute = check_entity_exists('node', 'institution', 'title', $institution_name);
        switch ($institute['operation']) {
          case 'edit':
            $user->field_institution['und'][$i]['target_id'] = $institute['data']->nid;
            $message = t(' -- Updating user institute(@institute_name) [SUCCESS]', array('@institute_name' => $institution_name));
            write_log($message, 'import_logs');
            $i++;
            break;

          case 'error':
            $message = t(' -- Updating user institute(@institute_name). Multiple institutions found. [ERROR]', array('@institute_name' => $institution_name));
            write_log($message, 'import_logs');
            break;

          case 'add':
            $message = t(' -- Updating user institute(@institute_name). Institution not found. [ERROR]', array('@institute_name' => $institution_name));
            write_log($message, 'import_logs');
            break;
        }
      }
    }
  }


  user_save($user);
  watchdog('user_migrate_data', '<pre>' . print_r($user, TRUE) . '</pre>');

  $context['finished'] = 1;
}

/**
 * Batch 'finished' callback used by both batch 1 and batch 2.
 */
function process_user_import_batch_op_finished($success, $results, $operations) {
  if ($success) {
    drupal_set_message(t('Operation successfully.'));
    unset($_SESSION['logname']['import_logs']);
  }
  else {
    // An error occurred.
    // $operations contains the operations that remained unprocessed.
    $error_operation = reset($operations);
    drupal_set_message(
      t('An error occurred while processing @operation with arguments : @args', array(
        '@operation' => $error_operation[0],
        '@args' => print_r($error_operation[0], TRUE),
      )), 'error');
  }
}
