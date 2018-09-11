<?php

function user_migrate_export_form($form, &$form_state) {
  $form['fieldset'] = array(
    '#title' => 'Export Users',
    '#type' => 'fieldset',
  );
  $form['fieldset']['export_fields'] = array(
    '#title' => "Include fields in export",
    '#type' => 'checkbox',
    '#default_value' => 1,
  );
  $form['fieldset']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Export'),
    '#submit' => array('user_migrate_callback'),
  );

  return $form;
}

function user_migrate_callback(&$form, &$form_state) {
  $json_temp_users = [];
  $data = db_select('users', 'users')
    ->fields('users')
    ->condition('uid', 1 , '!=') // We dont want admin user.
    ->condition('uid', 0 , '!=') // We dont want anonymous user.
    ->execute()->fetchAll();

  if (empty($data)) {
    // logging activity
    $message = t('No users to export.');
    write_log($message, 'export_logs');
    drupal_set_message($message, 'error');
  }
  else {
    watchdog('user_migrate_data', '<pre>' . print_r($data, TRUE) . '</pre>');

    // Batch to perform add/update availability data.
    $batch = process_user_export_batch($data, $form_state['values']);
    batch_set($batch);
  }
}

/********************* Export batch *********************/

/**
 * Batch foreach function.
 */
function process_user_export_batch($data, $form_data) {
  $operations = array();
  $i = 0;
  foreach ($data as $record) {
    $record = (array) $record;
    $operations[] = array(
      'process_user_export_batch_op',
      array(
        $record,
        $form_data['export_fields']
      ),
    );
    $i++;
  }
  $batch = array(
    'operations' => $operations,
    'finished' => 'process_user_export_batch_op_finished',
    'file' => drupal_get_path('module', 'user_migrate') . '/includes/user_migrate.export.inc',
  );
  return $batch;
}

/**
 * This is the function that is called on each operation in batch 1.
 */
function process_user_export_batch_op($record, $export_fields, &$context) {
  // Optional message displayed under the progressbar.
  $context['message'] = t('Exporting user: :name (:email)', array(':name' => $record['name'], ':email' => $record['mail']));
  $uid = $record['uid'];
  $user = user_load($uid);
  $export_user['core_details'] = $record;
  // logging activity
  $message = t('Exporting user: @name(@email - @uid) [SUCCESS]', array("@name" => $record['name'], '@email' => $record['mail'], '@uid' => $uid));
  write_log($message, 'export_logs');
  // Attach field based upon the requirement.
  if($export_fields == 1) {
    $fields = field_info_instances('user', 'user');
    foreach ($fields as $field) {
      $fieldkey = $field['field_name'];
      if(isset($user->$fieldkey['und'])) {
        switch ($field['display']['default']['module']) {
          case 'list':
            $export_user['fields'][$fieldkey] = $user->$fieldkey['und'][0]['value'];
            break;

          case 'text':
            $export_user['fields'][$fieldkey] = $user->$fieldkey['und'][0]['value'];
            break;

          case 'entityreference':
            foreach ($user->$fieldkey['und'] as $entity) {
              $entity = node_load($entity['target_id']);
              $export_user['fields'][$fieldkey][] = $entity->title;
            }
            break;
        }

        // logging activity
        $message = t(' -- Exporting @field_name [SUCCESS]', array('@field_name' => $field['label']));
        write_log($message, 'export_logs');
      }
      else {
        // logging activity
        $message = t(' -- Exporting @field_name (Empty field) [SUCCESS]', array('@field_name' => $field['label']));
        write_log($message, 'export_logs');
      }
    }
  }
  $export_user = json_encode($export_user);
  // $encrypted_data = encrypt($record);
  $context['results']['users'][$uid] = $export_user;
  $context['finished'] = 1;
}

/**
 * Batch 'finished' callback used by both batch 1 and batch 2.
 */
function process_user_export_batch_op_finished($success, $results, $operations) {
  if ($success) {
    watchdog('user_migrate_result', '<pre>' . print_r($results, TRUE) . '</pre>');
    $filename = "user_export.json";
    $file_folder_base = 'private://migrate_users/exports';
    file_prepare_directory($file_folder_base, FILE_CREATE_DIRECTORY);
    $filepath = $file_folder_base . '/' . $filename;
    file_put_contents($filepath, json_encode($results));
    unset($_SESSION['logname']['export_logs']);
    drupal_set_message(t('Operation successfully.'));
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
